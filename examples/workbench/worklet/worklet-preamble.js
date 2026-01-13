// AudioWorklet compatibility preamble
// AudioWorkletGlobalScope doesn't have location, so we need to provide a stub
if (typeof self !== 'undefined' && typeof self.location === 'undefined') {
    self.location = { href: '' };
}
if (typeof self === 'undefined' && typeof globalThis !== 'undefined') {
    var self = globalThis;
    self.location = { href: '' };
}
