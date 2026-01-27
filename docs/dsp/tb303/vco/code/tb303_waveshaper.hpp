// =============================================================================
// TB-303 Wave Shaper - SPICE-Accurate Implementation
// 
// Based on exact Falstad circuit simulation
// Uses Newton-Raphson nodal analysis with Ebers-Moll PNP transistor model
//
// Author: Claude (Anthropic)
// License: MIT
// =============================================================================

#pragma once

#include <cmath>
#include <algorithm>

namespace tb303 {

class WaveShaper {
public:
    // =========================================================================
    // Circuit Constants (from Falstad simulation)
    // =========================================================================
    
    // Power supplies
    static constexpr double V_CC = 12.0;      // +12V emitter supply
    static constexpr double V_COLL = 5.33;    // +5.33V collector supply
    
    // Resistors
    static constexpr double R1 = 10e3;        // 10kΩ input to GND (load only)
    static constexpr double R2 = 100e3;       // 100kΩ input to base
    static constexpr double R3 = 10e3;        // 10kΩ cap node to base
    static constexpr double R4 = 22e3;        // 22kΩ V_CC to emitter
    static constexpr double R5 = 10e3;        // 10kΩ collector to V_COLL
    
    // Capacitors
    static constexpr double C1 = 10e-9;       // 10nF memory capacitor
    static constexpr double C2 = 1e-6;        // 1µF emitter bypass
    
    // PNP Transistor parameters (Ebers-Moll model)
    static constexpr double V_T = 0.025865;   // Thermal voltage (kT/q at 300K)
    static constexpr double I_S = 1e-13;      // Saturation current
    static constexpr double BETA_F = 100.0;   // Forward current gain
    static constexpr double BETA_R = 0.5;     // Reverse current gain
    
    // Derived transistor parameters
    static constexpr double ALPHA_F = BETA_F / (BETA_F + 1.0);  // ~0.99
    static constexpr double ALPHA_R = BETA_R / (BETA_R + 1.0);  // ~0.33
    
    // Newton-Raphson parameters
    static constexpr int MAX_ITERATIONS = 100;
    static constexpr double CONVERGENCE_TOL = 1e-12;
    
private:
    // State variables (capacitor voltages)
    double v_c1_;    // Voltage across C1 (10nF): v_in - v_cap
    double v_c2_;    // Voltage across C2 (1µF): v_emitter to GND
    
    // Node voltages (for output/debugging)
    double v_cap_;   // Capacitor node voltage
    double v_b_;     // Base voltage
    double v_e_;     // Emitter voltage
    double v_c_;     // Collector voltage (output)
    
public:
    WaveShaper() { reset(); }
    
    /// Reset to initial state
    void reset() {
        v_c1_ = 0.0;
        v_c2_ = 8.0;   // Initial emitter voltage
        v_cap_ = 8.0;
        v_b_ = 8.0;
        v_e_ = 8.0;
        v_c_ = V_COLL;
    }
    
    /// Process one sample
    /// @param v_in Input voltage (typically 12V -> 5.5V falling sawtooth)
    /// @param dt Time step (1/sample_rate)
    /// @return Output voltage (collector)
    double process(double v_in, double dt) {
        // Conductances
        const double g2 = 1.0 / R2;
        const double g3 = 1.0 / R3;
        const double g4 = 1.0 / R4;
        const double g5 = 1.0 / R5;
        
        // Capacitor companion model conductances (backward Euler)
        const double g_c1 = C1 / dt;
        const double g_c2 = C2 / dt;
        
        // Previous capacitor voltages
        const double v_c1_prev = v_c1_;
        const double v_c2_prev = v_c2_;
        
        // Initial estimates for node voltages
        double v_cap = v_in - v_c1_prev;
        double v_b = v_b_;
        double v_e = v_c2_prev;
        double v_c = v_c_;
        
        // Newton-Raphson iteration
        for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
            // Compute transistor terminal currents
            double i_e, i_c, i_b;
            pnp_currents(v_e, v_b, v_c, i_e, i_c, i_b);
            
            // Compute transistor conductances
            const double v_eb = v_e - v_b;
            const double v_cb = v_c - v_b;
            const double g_ef = diode_conductance(v_eb);
            const double g_cr = diode_conductance(v_cb);
            
            // ================================================================
            // KCL equations (sum of currents INTO each node = 0)
            // ================================================================
            
            // Node v_cap: i_C1 - i_R3 = 0
            const double i_c1 = g_c1 * (v_in - v_cap - v_c1_prev);
            const double i_r3 = g3 * (v_cap - v_b);
            const double f1 = i_c1 - i_r3;
            
            // Node v_b: i_R2 + i_R3 + i_B = 0
            const double i_r2 = g2 * (v_in - v_b);
            const double f2 = i_r2 + i_r3 + i_b;
            
            // Node v_e: i_R4 - i_E - i_C2 = 0
            const double i_r4 = g4 * (V_CC - v_e);
            const double i_c2 = g_c2 * (v_e - v_c2_prev);
            const double f3 = i_r4 - i_e - i_c2;
            
            // Node v_c: i_R5 + i_C = 0
            const double i_r5 = g5 * (V_COLL - v_c);
            const double f4 = i_r5 + i_c;
            
            // Check convergence
            const double err = std::abs(f1) + std::abs(f2) + std::abs(f3) + std::abs(f4);
            if (err < CONVERGENCE_TOL) break;
            
            // ================================================================
            // Jacobian matrix computation
            // ================================================================
            
            // Transistor current derivatives
            const double dib_dvb = -(1.0 - ALPHA_F) * g_ef - (1.0 - ALPHA_R) * g_cr;
            const double dib_dve = (1.0 - ALPHA_F) * g_ef;
            const double dib_dvc = (1.0 - ALPHA_R) * g_cr;
            
            const double die_dvb = -g_ef + ALPHA_R * g_cr;
            const double die_dve = g_ef;
            const double die_dvc = -ALPHA_R * g_cr;
            
            const double dic_dvb = -ALPHA_F * g_ef + g_cr;
            const double dic_dve = ALPHA_F * g_ef;
            const double dic_dvc = -g_cr;
            
            // Jacobian J[i][j] = df_i / dv_j
            // Variables: [v_cap, v_b, v_e, v_c]
            double J[4][4] = {
                {-g_c1 - g3,     g3,                  0.0,                    0.0},
                {g3,            -g2 - g3 + dib_dvb,   dib_dve,                dib_dvc},
                {0.0,           -die_dvb,            -g4 - die_dve - g_c2,   -die_dvc},
                {0.0,            dic_dvb,             dic_dve,               -g5 + dic_dvc}
            };
            
            // Solve J * dv = -f using Gaussian elimination with partial pivoting
            double b[4] = {-f1, -f2, -f3, -f4};
            double dv[4];
            solve_4x4(J, b, dv);
            
            // Damped Newton update (limit step size for stability)
            double max_dv = 0.0;
            for (int i = 0; i < 4; i++) {
                max_dv = std::max(max_dv, std::abs(dv[i]));
            }
            const double damp = (max_dv > 0.1) ? 0.1 / max_dv : 1.0;
            
            v_cap += damp * dv[0];
            v_b   += damp * dv[1];
            v_e   += damp * dv[2];
            v_c   += damp * dv[3];
            
            // Clamp to reasonable bounds
            v_e = std::clamp(v_e, 0.0, V_CC + 0.5);
            v_c = std::clamp(v_c, 0.0, V_CC + 0.5);
        }
        
        // Update capacitor states
        v_c1_ = v_in - v_cap;
        v_c2_ = v_e;
        
        // Store node voltages for getters
        v_cap_ = v_cap;
        v_b_ = v_b;
        v_e_ = v_e;
        v_c_ = v_c;
        
        return v_c;
    }
    
    // =========================================================================
    // Getters for debugging/visualization
    // =========================================================================
    
    double getCapNodeVoltage() const { return v_cap_; }
    double getBaseVoltage() const { return v_b_; }
    double getEmitterVoltage() const { return v_e_; }
    double getCollectorVoltage() const { return v_c_; }
    double getCapVoltage() const { return v_c1_; }
    
private:
    // =========================================================================
    // Diode model (with numerical protection)
    // =========================================================================
    
    /// Diode current: I = Is * (exp(V/Vt) - 1)
    static double diode_current(double v) {
        constexpr double v_crit = V_T * 40.0;  // ~1V, linearize above this
        
        if (v > v_crit) {
            // Linear extrapolation to prevent overflow
            const double i_crit = I_S * (std::exp(v_crit / V_T) - 1.0);
            const double g_crit = I_S / V_T * std::exp(v_crit / V_T);
            return i_crit + g_crit * (v - v_crit);
        }
        if (v < -10.0 * V_T) {
            return -I_S;
        }
        return I_S * (std::exp(v / V_T) - 1.0);
    }
    
    /// Diode conductance: dI/dV
    static double diode_conductance(double v) {
        constexpr double v_crit = V_T * 40.0;
        constexpr double g_min = 1e-12;
        
        if (v > v_crit) {
            return I_S / V_T * std::exp(v_crit / V_T);
        }
        if (v < -10.0 * V_T) {
            return g_min;
        }
        return I_S / V_T * std::exp(v / V_T) + g_min;
    }
    
    // =========================================================================
    // PNP Transistor model (Ebers-Moll)
    // =========================================================================
    
    /// Compute PNP terminal currents
    /// Convention: currents defined as flowing INTO the terminal
    /// For conducting PNP: I_E > 0, I_C < 0, I_B < 0
    static void pnp_currents(double v_e, double v_b, double v_c,
                             double& i_e, double& i_c, double& i_b) {
        const double v_eb = v_e - v_b;  // Emitter-Base voltage
        const double v_cb = v_c - v_b;  // Collector-Base voltage
        
        const double i_ef = diode_current(v_eb);  // E-B junction current
        const double i_cr = diode_current(v_cb);  // C-B junction current
        
        // Ebers-Moll equations
        i_e = i_ef - ALPHA_R * i_cr;
        i_c = ALPHA_F * i_ef - i_cr;
        i_b = i_e - i_c;
    }
    
    // =========================================================================
    // Linear algebra (4x4 Gaussian elimination)
    // =========================================================================
    
    /// Solve 4x4 linear system: J * x = b
    static void solve_4x4(double J[4][4], double b[4], double x[4]) {
        // Augmented matrix
        double A[4][5];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) A[i][j] = J[i][j];
            A[i][4] = b[i];
        }
        
        // Forward elimination with partial pivoting
        for (int i = 0; i < 4; i++) {
            // Find pivot
            int maxRow = i;
            for (int k = i + 1; k < 4; k++) {
                if (std::abs(A[k][i]) > std::abs(A[maxRow][i])) {
                    maxRow = k;
                }
            }
            
            // Swap rows
            for (int j = 0; j < 5; j++) {
                std::swap(A[i][j], A[maxRow][j]);
            }
            
            // Prevent division by zero
            if (std::abs(A[i][i]) < 1e-30) A[i][i] = 1e-30;
            
            // Eliminate column
            for (int k = i + 1; k < 4; k++) {
                const double factor = A[k][i] / A[i][i];
                for (int j = i; j < 5; j++) {
                    A[k][j] -= factor * A[i][j];
                }
            }
        }
        
        // Back substitution
        for (int i = 3; i >= 0; i--) {
            x[i] = A[i][4];
            for (int j = i + 1; j < 4; j++) {
                x[i] -= A[i][j] * x[j];
            }
            x[i] /= A[i][i];
        }
    }
};

} // namespace tb303
