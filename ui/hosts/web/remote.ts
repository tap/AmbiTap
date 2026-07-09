/// Browser-as-remote-surface (UI.md wave 3): mirror widget edits as binary
/// OSC over a WebSocket to scripts/osc-bridge.mjs, which relays to UDP for
/// [udpreceive] in Max (or any OSC host). Address space:
///
///   /ambitap/source/<id>/direction  ff   azimuth elevation   (radians)
///   /ambitap/orientation            fff  yaw pitch roll      (radians)
///
/// Reconnects with a fixed backoff; sends are dropped while disconnected
/// (a control surface must never buffer stale gestures).

import { encodeOsc } from '../../core/osc.js';

const RECONNECT_MS = 2000;

export class RemoteSurface {
    private socket: WebSocket | null = null;
    private closed = false;

    constructor(private readonly url: string, private readonly onStatus: (text: string) => void = () => {}) {
        this.connect();
    }

    private connect(): void {
        if (this.closed) {
            return;
        }
        this.onStatus(`connecting ${this.url}`);
        const socket = new WebSocket(this.url);
        socket.binaryType = 'arraybuffer';
        socket.onopen = () => this.onStatus(`remote: ${this.url}`);
        socket.onclose = () => {
            this.socket = null;
            this.onStatus('remote: disconnected, retrying');
            setTimeout(() => this.connect(), RECONNECT_MS);
        };
        socket.onerror = () => socket.close();
        this.socket = socket;
    }

    private send(address: string, args: number[]): void {
        if (this.socket?.readyState === WebSocket.OPEN) {
            this.socket.send(encodeOsc(address, args));
        }
    }

    sendDirection(id: string, azimuth: number, elevation: number): void {
        this.send(`/ambitap/source/${id}/direction`, [azimuth, elevation]);
    }

    sendOrientation(yaw: number, pitch: number, roll: number): void {
        this.send('/ambitap/orientation', [yaw, pitch, roll]);
    }

    close(): void {
        this.closed = true;
        this.socket?.close();
    }
}
