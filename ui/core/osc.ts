/// Minimal OSC 1.0 message codec — enough for the remote-surface path
/// (float arguments only on the send side; the decoder also reads ints and
/// strings for tests and future use). Binary OSC travels over WebSocket to
/// scripts/osc-bridge.mjs, which relays the bytes verbatim to UDP for
/// [udpreceive] in Max.

export type OscArg = number | string;

/** Padded size of an UNTERMINATED string of `length` chars: the NUL plus
 *  alignment to the next 4-byte boundary (so a 4-char string takes 8). */
function padded(length: number): number {
    return (length + 4) & ~3;
}

function writeString(view: DataView, offset: number, text: string): number {
    for (let i = 0; i < text.length; ++i) {
        view.setUint8(offset + i, text.charCodeAt(i) & 0x7f);
    }
    const end = padded(text.length);
    for (let i = text.length; i < end; ++i) {
        view.setUint8(offset + i, 0);
    }
    return offset + end;
}

/** Encode one OSC message; all numeric args are sent as float32. */
export function encodeOsc(address: string, args: number[]): Uint8Array {
    const size = padded(address.length) + padded(1 + args.length) + args.length * 4;
    const bytes = new Uint8Array(size);
    const view = new DataView(bytes.buffer);
    let offset = writeString(view, 0, address);
    offset = writeString(view, offset, ',' + 'f'.repeat(args.length));
    for (const arg of args) {
        view.setFloat32(offset, arg, false); // big-endian
        offset += 4;
    }
    return bytes;
}

function readString(view: DataView, offset: number): { value: string; next: number } {
    let end = offset;
    while (view.getUint8(end) !== 0) {
        ++end;
    }
    let value = '';
    for (let i = offset; i < end; ++i) {
        value += String.fromCharCode(view.getUint8(i));
    }
    return { value, next: offset + padded(end - offset) };
}

/** Decode one OSC message (f/i/s arguments). */
export function decodeOsc(bytes: Uint8Array): { address: string; args: OscArg[] } {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const addr = readString(view, 0);
    const tags = readString(view, addr.next);
    if (!tags.value.startsWith(',')) {
        throw new Error('OSC: missing type tag string');
    }
    const args: OscArg[] = [];
    let offset = tags.next;
    for (const tag of tags.value.slice(1)) {
        if (tag === 'f') {
            args.push(view.getFloat32(offset, false));
            offset += 4;
        } else if (tag === 'i') {
            args.push(view.getInt32(offset, false));
            offset += 4;
        } else if (tag === 's') {
            const s = readString(view, offset);
            args.push(s.value);
            offset = s.next;
        } else {
            throw new Error(`OSC: unsupported type tag '${tag}'`);
        }
    }
    return { address: addr.value, args };
}
