// WebSocket -> UDP OSC relay: browsers cannot send UDP, so the remote
// surface (hosts/web/remote.ts) sends binary OSC frames over a WebSocket
// and this bridge forwards each frame verbatim as one UDP datagram —
// straight into [udpreceive <port>] in Max:
//
//   node scripts/osc-bridge.mjs [--listen 8090] [--send 127.0.0.1:7500]
//
//   [udpreceive 7500] -> [route /ambitap/orientation] -> [unpack 0. 0. 0.]
//     -> yaw/pitch/roll into ambitap.rotate~ / the rotation v8ui.
import { createSocket } from 'node:dgram';
import { WebSocketServer } from 'ws';

const args = process.argv.slice(2);
function opt(name, fallback) {
    const i = args.indexOf(name);
    return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
}
const listenPort = Number(opt('--listen', '8090'));
const [sendHost, sendPort] = opt('--send', '127.0.0.1:7500').split(':');

const udp = createSocket('udp4');
const wss = new WebSocketServer({ port: listenPort });
console.log(`osc-bridge: ws://localhost:${listenPort} -> udp ${sendHost}:${sendPort}`);

wss.on('connection', (ws, req) => {
    console.log(`osc-bridge: client ${req.socket.remoteAddress} connected`);
    ws.on('message', (data, isBinary) => {
        if (isBinary) {
            udp.send(data, Number(sendPort), sendHost);
        }
    });
    ws.on('close', () => console.log('osc-bridge: client disconnected'));
});
