// AudioWorklet compatibility preamble
// AudioWorkletGlobalScope doesn't have location or atob, so we need to provide stubs

if (typeof self !== 'undefined' && typeof self.location === 'undefined') {
    self.location = { href: '' };
}
if (typeof self === 'undefined' && typeof globalThis !== 'undefined') {
    var self = globalThis;
    self.location = { href: '' };
}

// Provide atob for AudioWorkletGlobalScope (not available in worklet context)
if (typeof atob === 'undefined') {
    var atob = function(base64) {
        const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
        let result = '';
        let bufferLength = base64.length * 0.75;
        if (base64[base64.length - 1] === '=') bufferLength--;
        if (base64[base64.length - 2] === '=') bufferLength--;

        const bytes = new Uint8Array(bufferLength);
        let p = 0;

        for (let i = 0; i < base64.length; i += 4) {
            const e1 = chars.indexOf(base64[i]);
            const e2 = chars.indexOf(base64[i + 1]);
            const e3 = chars.indexOf(base64[i + 2]);
            const e4 = chars.indexOf(base64[i + 3]);

            bytes[p++] = (e1 << 2) | (e2 >> 4);
            if (e3 !== -1 && base64[i + 2] !== '=') bytes[p++] = ((e2 & 15) << 4) | (e3 >> 2);
            if (e4 !== -1 && base64[i + 3] !== '=') bytes[p++] = ((e3 & 3) << 6) | e4;
        }

        // Convert to string (standard atob returns a string)
        let str = '';
        for (let i = 0; i < bytes.length; i++) {
            str += String.fromCharCode(bytes[i]);
        }
        return str;
    };
}
