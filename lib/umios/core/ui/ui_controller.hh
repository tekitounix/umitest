// SPDX-License-Identifier: MIT
// UMI-OS - UI Controller (UI Logic & State)
//
// UIController manages UI state and logic, separate from:
// - Processor: Audio DSP (no UI knowledge)
// - UIMap: Control-Parameter bindings
// - View/Skin: Actual rendering (platform-specific)
//
// Responsibilities:
// - Parameter state cache (bidirectional sync with Processor)
// - Preset management coordination
// - Undo/Redo history
// - MIDI Learn orchestration
// - Automation recording/playback

#pragma once

#include "ui_map.hh"
#include "processor.hh"
#include "event.hh"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>
#include <string>

namespace umi {

// ============================================================================
// Parameter Change - Represents a parameter value change
// ============================================================================

struct ParamChange {
    uint32_t param_id;
    float old_value;
    float new_value;
    uint64_t timestamp;
};

// ============================================================================
// View Interface - Abstract interface for UI rendering
// ============================================================================

/// Interface that Views/Skins must implement
/// This is platform-agnostic - implementations are in adapters
struct IView {
    virtual ~IView() = default;
    
    /// Called when a parameter value changes (update knob position, etc.)
    virtual void on_param_changed(uint32_t param_id, float normalized_value) = 0;
    
    /// Called when preset changes
    virtual void on_preset_changed(std::string_view name) = 0;
    
    /// Called when MIDI Learn mode changes
    virtual void on_midi_learn_state(bool is_learning, std::optional<uint32_t> param_id) = 0;
    
    /// Request full UI refresh
    virtual void refresh() = 0;
};

// ============================================================================
// UIController - Central UI logic and state management
// ============================================================================

template<typename Processor, typename UIMapT>
class UIController {
public:
    /// Construct with processor reference, UI map, and optional MIDI map
    UIController(Processor& processor, const UIMapT& ui_map)
        : processor_(processor)
        , ui_map_(ui_map)
    {
        // Initialize parameter cache from processor descriptors
        if constexpr (requires { Processor::param_descriptors(); }) {
            auto descs = Processor::param_descriptors();
            param_cache_.resize(descs.size());
            for (const auto& d : descs) {
                if (d.id < param_cache_.size()) {
                    param_cache_[d.id] = d.default_value;
                }
            }
        }
    }
    
    /// Set MIDI map (can be static or dynamic)
    template<typename MidiMapT>
    void set_midi_map(MidiMapT* midi_map) {
        // Store as type-erased interface for CC lookup
        find_midi_ = [midi_map](uint8_t ch, uint8_t cc) -> const MidiMapping* {
            return midi_map->find(ch, cc);
        };
    }
    
    /// Set dynamic MIDI map (for MIDI Learn support)
    void set_midi_map(MidiMapDynamic* midi_map) {
        midi_map_dynamic_ = midi_map;
        find_midi_ = [midi_map](uint8_t ch, uint8_t cc) -> const MidiMapping* {
            return midi_map->find(ch, cc);
        };
    }
    
    /// Attach a view for UI updates
    void attach_view(IView* view) {
        view_ = view;
    }
    
    void detach_view() {
        view_ = nullptr;
    }
    
    // ========================================================================
    // Parameter Control (from UI)
    // ========================================================================
    
    /// Set parameter from UI control (by control ID)
    void set_from_control(std::string_view control_id, float normalized) {
        if (auto* mapping = ui_map_.find_control(control_id)) {
            float display_value = mapping->to_display(normalized);
            set_param(mapping->param_id, display_value);
        }
    }
    
    /// Set parameter directly (by param ID, display value)
    void set_param(uint32_t param_id, float value) {
        // Record for undo if enabled
        if (recording_undo_ && param_id < param_cache_.size()) {
            record_undo(param_id, param_cache_[param_id], value);
        }
        
        // Update cache
        if (param_id < param_cache_.size()) {
            param_cache_[param_id] = value;
        }
        
        // Send to processor
        processor_.set_param(param_id, value);
        
        // Notify view
        if (view_) {
            if (auto* mapping = ui_map_.find_by_param(param_id)) {
                float normalized = mapping->from_display(value);
                view_->on_param_changed(param_id, normalized);
            }
        }
    }
    
    /// Get parameter value (from cache)
    float get_param(uint32_t param_id) const {
        if (param_id < param_cache_.size()) {
            return param_cache_[param_id];
        }
        return 0.0f;
    }
    
    /// Get normalized value for display
    float get_normalized(uint32_t param_id) const {
        if (auto* mapping = ui_map_.find_by_param(param_id)) {
            return mapping->from_display(get_param(param_id));
        }
        return 0.0f;
    }
    
    // ========================================================================
    // MIDI Learn (requires MidiMapDynamic)
    // ========================================================================
    
    /// Start MIDI learn for a parameter
    void start_midi_learn(uint32_t param_id) {
        if (midi_map_dynamic_) {
            midi_map_dynamic_->start_learn(param_id);
            if (view_) {
                view_->on_midi_learn_state(true, param_id);
            }
        }
    }
    
    /// Cancel MIDI learn
    void cancel_midi_learn() {
        if (midi_map_dynamic_) {
            midi_map_dynamic_->cancel_learn();
            if (view_) {
                view_->on_midi_learn_state(false, std::nullopt);
            }
        }
    }
    
    /// Process incoming MIDI CC
    void process_midi_cc(uint8_t channel, uint8_t cc, uint8_t value) {
        // Check MIDI learn mode first
        if (midi_map_dynamic_ && midi_map_dynamic_->is_learning()) {
            midi_map_dynamic_->learn(channel, cc);
            if (view_) {
                view_->on_midi_learn_state(false, std::nullopt);
            }
            return;
        }
        
        // Normal CC mapping
        if (find_midi_) {
            if (auto* midi_map = find_midi_(channel, cc)) {
                float normalized = midi_map->cc_to_normalized(value);
                if (auto* ctrl_map = ui_map_.find_by_param(midi_map->param_id)) {
                    float display = ctrl_map->to_display(normalized);
                    set_param(midi_map->param_id, display);
                }
            }
        }
    }
    
    // ========================================================================
    // Undo/Redo
    // ========================================================================
    
    void enable_undo(bool enable) { recording_undo_ = enable; }
    
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    
    void undo() {
        if (undo_stack_.empty()) return;
        
        auto change = undo_stack_.back();
        undo_stack_.pop_back();
        
        // Apply old value without recording
        bool was_recording = recording_undo_;
        recording_undo_ = false;
        set_param(change.param_id, change.old_value);
        recording_undo_ = was_recording;
        
        redo_stack_.push_back(change);
    }
    
    void redo() {
        if (redo_stack_.empty()) return;
        
        auto change = redo_stack_.back();
        redo_stack_.pop_back();
        
        bool was_recording = recording_undo_;
        recording_undo_ = false;
        set_param(change.param_id, change.new_value);
        recording_undo_ = was_recording;
        
        undo_stack_.push_back(change);
    }
    
    void clear_undo_history() {
        undo_stack_.clear();
        redo_stack_.clear();
    }
    
    // ========================================================================
    // Preset Support
    // ========================================================================
    
    /// Get all parameter values as state
    std::vector<float> get_state() const {
        return param_cache_;
    }
    
    /// Set all parameter values from state
    void set_state(std::span<const float> state) {
        clear_undo_history();
        
        for (size_t i = 0; i < state.size() && i < param_cache_.size(); ++i) {
            param_cache_[i] = state[i];
            processor_.set_param(static_cast<uint32_t>(i), state[i]);
        }
        
        // Refresh entire view
        if (view_) {
            view_->refresh();
        }
    }
    
    /// Notify view of preset change
    void notify_preset_changed(std::string_view name) {
        current_preset_name_ = name;
        if (view_) {
            view_->on_preset_changed(name);
        }
    }
    
    std::string_view current_preset() const { return current_preset_name_; }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    const UIMapT& ui_map() const { return ui_map_; }
    Processor& processor() { return processor_; }
    const Processor& processor() const { return processor_; }
    
private:
    void record_undo(uint32_t param_id, float old_val, float new_val) {
        // Clear redo stack on new action
        redo_stack_.clear();
        
        // Coalesce rapid changes to same parameter
        if (!undo_stack_.empty() && undo_stack_.back().param_id == param_id) {
            undo_stack_.back().new_value = new_val;
        } else {
            undo_stack_.push_back({
                .param_id = param_id,
                .old_value = old_val,
                .new_value = new_val,
                .timestamp = 0,  // TODO: Add timestamp
            });
        }
        
        // Limit history size
        constexpr size_t kMaxUndoHistory = 100;
        if (undo_stack_.size() > kMaxUndoHistory) {
            undo_stack_.erase(undo_stack_.begin());
        }
    }
    
    Processor& processor_;
    const UIMapT& ui_map_;
    IView* view_ = nullptr;
    
    // MIDI map (type-erased for flexibility)
    std::function<const MidiMapping*(uint8_t, uint8_t)> find_midi_;
    MidiMapDynamic* midi_map_dynamic_ = nullptr;  // For MIDI Learn
    
    std::vector<float> param_cache_;
    std::vector<ParamChange> undo_stack_;
    std::vector<ParamChange> redo_stack_;
    bool recording_undo_ = false;
    
    std::string current_preset_name_;
};

} // namespace umi
