// SPDX-License-Identifier: MIT
// UMI-OS - View/Skin Interface
//
// This header defines the abstract interface for UI rendering.
// Concrete implementations are platform-specific:
// - Web: DOM/Canvas manipulation (in adapter/wasm/)
// - Desktop: VSTGUI, JUCE, Qt (in adapter/vst3/, etc.)
// - Hardware: Physical controls, LEDs (in adapter/hw/)
//
// The View is "dumb" - it only renders and reports input.
// All logic lives in UIController.

#pragma once

#include "ui_controller.hh"

#include <cstdint>
#include <string_view>
#include <functional>
#include <optional>

namespace umi {

// ============================================================================
// Control Event - Input from View to Controller
// ============================================================================

enum class ControlEventType : uint8_t {
    ValueChange,      // Knob/slider moved
    ButtonPress,      // Button pressed
    ButtonRelease,    // Button released
    BeginEdit,        // Start of gesture (for undo grouping)
    EndEdit,          // End of gesture
};

struct ControlEvent {
    ControlEventType type;
    std::string_view control_id;  // Which control
    float value;                  // Normalized [0,1] for continuous
};

// ============================================================================
// ViewBase - Base class for platform-specific views
// ============================================================================

/// Optional base class providing common view functionality
template<typename Controller>
class ViewBase : public IView {
public:
    explicit ViewBase(Controller& controller)
        : controller_(controller)
    {
        controller_.attach_view(this);
    }
    
    ~ViewBase() override {
        controller_.detach_view();
    }
    
    // IView interface - override in derived classes
    void on_param_changed(uint32_t param_id, float normalized) override {
        // Default: do nothing. Override to update UI.
    }
    
    void on_preset_changed(std::string_view name) override {
        // Default: do nothing. Override to update preset display.
    }
    
    void on_midi_learn_state(bool learning, std::optional<uint32_t> param) override {
        // Default: do nothing. Override to show learn indicator.
    }
    
    void refresh() override {
        // Default: do nothing. Override to refresh all controls.
    }
    
protected:
    /// Call this when user interacts with a control
    void on_control_event(const ControlEvent& event) {
        switch (event.type) {
            case ControlEventType::ValueChange:
                controller_.set_from_control(event.control_id, event.value);
                break;
            case ControlEventType::BeginEdit:
                controller_.enable_undo(true);
                break;
            case ControlEventType::EndEdit:
                // Could group undo here
                break;
            default:
                break;
        }
    }
    
    /// Start MIDI learn for control
    void start_learn(std::string_view control_id) {
        if (auto* mapping = controller_.map().find_control(control_id)) {
            controller_.start_midi_learn(mapping->param_id);
        }
    }
    
    Controller& controller() { return controller_; }
    const Controller& controller() const { return controller_; }
    
private:
    Controller& controller_;
};

// ============================================================================
// NullView - No-op view for headless operation
// ============================================================================

struct NullView : IView {
    void on_param_changed(uint32_t, float) override {}
    void on_preset_changed(std::string_view) override {}
    void on_midi_learn_state(bool, std::optional<uint32_t>) override {}
    void refresh() override {}
};

} // namespace umi
