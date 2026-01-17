/**
 * UMI Web - Theme System
 *
 * CSS variable-based theming for consistent styling.
 *
 * @module umi_web/theme
 */

/**
 * Default dark theme
 */
export const darkTheme = {
    // Base colors
    '--umi-bg-primary': '#0a0a15',
    '--umi-bg-secondary': '#1a1a2e',
    '--umi-bg-tertiary': '#252540',
    '--umi-bg-hover': 'rgba(255, 255, 255, 0.05)',
    '--umi-bg-active': 'rgba(255, 255, 255, 0.1)',

    // Text colors
    '--umi-text-primary': '#ffffff',
    '--umi-text-secondary': '#cccccc',
    '--umi-text-muted': '#888888',
    '--umi-text-disabled': '#666666',

    // Accent colors
    '--umi-accent': '#4ecca3',
    '--umi-accent-hover': '#3db892',
    '--umi-accent-dark': '#2a8a6e',

    // Semantic colors
    '--umi-warning': '#f0a500',
    '--umi-error': '#ff6b6b',
    '--umi-success': '#4ecca3',
    '--umi-info': '#5bc0de',

    // Border
    '--umi-border': '#1a1a2e',
    '--umi-border-light': '#252540',

    // Keyboard specific
    '--umi-key-white': 'linear-gradient(to bottom, #e8e8e8 0%, #fff 100%)',
    '--umi-key-white-border': '#888888',
    '--umi-key-black': 'linear-gradient(to bottom, #333333 0%, #000000 100%)',
    '--umi-key-black-border': '#000000',
    '--umi-key-active': 'linear-gradient(to bottom, #4ecca3 0%, #3db892 100%)',
    '--umi-key-label': '#666666',

    // Shadows
    '--umi-shadow': '0 2px 8px rgba(0, 0, 0, 0.3)',

    // Radius
    '--umi-radius-sm': '2px',
    '--umi-radius': '4px',
    '--umi-radius-lg': '8px',
};

/**
 * Light theme
 */
export const lightTheme = {
    // Base colors
    '--umi-bg-primary': '#ffffff',
    '--umi-bg-secondary': '#f5f5f5',
    '--umi-bg-tertiary': '#e8e8e8',
    '--umi-bg-hover': 'rgba(0, 0, 0, 0.05)',
    '--umi-bg-active': 'rgba(0, 0, 0, 0.1)',

    // Text colors
    '--umi-text-primary': '#1a1a2e',
    '--umi-text-secondary': '#333333',
    '--umi-text-muted': '#666666',
    '--umi-text-disabled': '#999999',

    // Accent colors
    '--umi-accent': '#2a8a6e',
    '--umi-accent-hover': '#238060',
    '--umi-accent-dark': '#1a6a50',

    // Semantic colors
    '--umi-warning': '#e09000',
    '--umi-error': '#d32f2f',
    '--umi-success': '#2a8a6e',
    '--umi-info': '#1976d2',

    // Border
    '--umi-border': '#e0e0e0',
    '--umi-border-light': '#f0f0f0',

    // Keyboard specific
    '--umi-key-white': 'linear-gradient(to bottom, #ffffff 0%, #f0f0f0 100%)',
    '--umi-key-white-border': '#cccccc',
    '--umi-key-black': 'linear-gradient(to bottom, #404040 0%, #1a1a1a 100%)',
    '--umi-key-black-border': '#000000',
    '--umi-key-active': 'linear-gradient(to bottom, #4ecca3 0%, #3db892 100%)',
    '--umi-key-label': '#888888',

    // Shadows
    '--umi-shadow': '0 2px 8px rgba(0, 0, 0, 0.1)',

    // Radius
    '--umi-radius-sm': '2px',
    '--umi-radius': '4px',
    '--umi-radius-lg': '8px',
};

/**
 * Apply theme to document
 * @param {object} theme - Theme object with CSS variable values
 * @param {HTMLElement} [root=document.documentElement]
 */
export function applyTheme(theme, root = document.documentElement) {
    for (const [key, value] of Object.entries(theme)) {
        root.style.setProperty(key, value);
    }
}

/**
 * Get CSS for theme variables (for embedding)
 * @param {object} theme
 * @param {string} [selector=':root']
 * @returns {string}
 */
export function getThemeCSS(theme, selector = ':root') {
    const vars = Object.entries(theme)
        .map(([key, value]) => `    ${key}: ${value};`)
        .join('\n');
    return `${selector} {\n${vars}\n}`;
}

/**
 * Get base component styles using CSS variables
 * @returns {string}
 */
export function getBaseStyles() {
    return `
/* UMI Web - Base Component Styles */

/* Waveform */
.umi-waveform {
    background: var(--umi-bg-primary);
    border: 1px solid var(--umi-border);
    border-radius: var(--umi-radius);
}

/* Keyboard */
.umi-keyboard {
    display: flex;
    position: relative;
    height: 120px;
    user-select: none;
}
.umi-keyboard .key {
    position: relative;
    cursor: pointer;
    transition: background 0.05s;
}
.umi-keyboard .key.white {
    width: 40px;
    height: 100%;
    background: var(--umi-key-white);
    border: 1px solid var(--umi-key-white-border);
    border-radius: 0 0 var(--umi-radius) var(--umi-radius);
    z-index: 1;
}
.umi-keyboard .key.black {
    width: 24px;
    height: 60%;
    background: var(--umi-key-black);
    border: 1px solid var(--umi-key-black-border);
    border-radius: 0 0 var(--umi-radius-sm) var(--umi-radius-sm);
    margin-left: -12px;
    margin-right: -12px;
    z-index: 2;
}
.umi-keyboard .key.white.active,
.umi-keyboard .key.black.active {
    background: var(--umi-key-active);
}
.umi-keyboard .key-label {
    position: absolute;
    bottom: 8px;
    left: 50%;
    transform: translateX(-50%);
    font-size: 10px;
    color: var(--umi-key-label);
    pointer-events: none;
}

/* Parameter Control */
.umi-param-control .param {
    padding: 8px;
    border-radius: var(--umi-radius);
    background: var(--umi-bg-hover);
    cursor: pointer;
}
.umi-param-control .param:hover {
    background: var(--umi-bg-active);
}
.umi-param-control .param.learn-target {
    outline: 2px solid var(--umi-warning);
    background: rgba(240, 165, 0, 0.1);
}
.umi-param-control.midi-learn-mode .param {
    cursor: crosshair;
}
.umi-param-control .param-label {
    display: flex;
    justify-content: space-between;
    margin-bottom: 4px;
    font-size: 12px;
}
.umi-param-control .param-name {
    color: var(--umi-text-muted);
}
.umi-param-control .param-value {
    color: var(--umi-accent);
    font-family: monospace;
}
.umi-param-control input[type="range"] {
    width: 100%;
    height: 4px;
    -webkit-appearance: none;
    background: var(--umi-bg-secondary);
    border-radius: var(--umi-radius-sm);
    outline: none;
}
.umi-param-control input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 12px;
    height: 12px;
    background: var(--umi-accent);
    border-radius: 50%;
    cursor: pointer;
}
.umi-param-control .param-mapping {
    font-size: 10px;
    color: var(--umi-warning);
    margin-top: 4px;
    min-height: 14px;
}
.umi-param-control .param-mapping .clear-btn {
    cursor: pointer;
    color: var(--umi-text-muted);
}
.umi-param-control .param-mapping .clear-btn:hover {
    color: var(--umi-error);
}

/* MIDI Monitor */
.umi-midi-monitor {
    font-family: monospace;
    font-size: 11px;
    background: var(--umi-bg-primary);
    border: 1px solid var(--umi-border);
    border-radius: var(--umi-radius);
    overflow: hidden;
}
.umi-midi-monitor .midi-monitor-list {
    max-height: 200px;
    overflow-y: auto;
    padding: 4px;
}
.umi-midi-monitor .midi-message {
    display: flex;
    gap: 8px;
    padding: 2px 4px;
    border-radius: var(--umi-radius-sm);
}
.umi-midi-monitor .midi-message:hover {
    background: var(--umi-bg-hover);
}
.umi-midi-monitor .midi-message.in .dir {
    color: var(--umi-accent);
}
.umi-midi-monitor .midi-message.out .dir {
    color: var(--umi-warning);
}
.umi-midi-monitor .midi-message .time {
    color: var(--umi-text-disabled);
    min-width: 85px;
}
.umi-midi-monitor .midi-message .msg {
    color: var(--umi-text-secondary);
}

/* Shell */
.umi-shell {
    display: flex;
    flex-direction: column;
    font-family: monospace;
    font-size: 12px;
    background: var(--umi-bg-primary);
    border: 1px solid var(--umi-border);
    border-radius: var(--umi-radius);
    overflow: hidden;
}
.umi-shell.disabled {
    opacity: 0.5;
}
.umi-shell .shell-output {
    flex: 1;
    min-height: 100px;
    max-height: 300px;
    overflow-y: auto;
    padding: 8px;
}
.umi-shell .shell-entry {
    white-space: pre-wrap;
    word-break: break-all;
    margin-bottom: 2px;
}
.umi-shell .shell-entry.stdout {
    color: var(--umi-text-secondary);
}
.umi-shell .shell-entry.stderr {
    color: var(--umi-error);
}
.umi-shell .shell-entry.command {
    color: var(--umi-accent);
}
.umi-shell .shell-entry.system {
    color: var(--umi-text-muted);
    font-style: italic;
}
.umi-shell .shell-input-wrapper {
    display: flex;
    align-items: center;
    padding: 8px;
    border-top: 1px solid var(--umi-border);
    background: var(--umi-bg-hover);
}
.umi-shell .shell-prompt {
    color: var(--umi-accent);
    margin-right: 4px;
}
.umi-shell .shell-input {
    flex: 1;
    background: transparent;
    border: none;
    color: var(--umi-text-primary);
    font-family: inherit;
    font-size: inherit;
    outline: none;
}
.umi-shell .shell-input::placeholder {
    color: var(--umi-text-disabled);
}
.umi-shell .shell-input:disabled {
    color: var(--umi-text-disabled);
}
`;
}

/**
 * Get complete styles (theme + base)
 * @param {object} [theme=darkTheme]
 * @returns {string}
 */
export function getCompleteStyles(theme = darkTheme) {
    return getThemeCSS(theme) + '\n' + getBaseStyles();
}
