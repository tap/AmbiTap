// Minimal ambient declarations for the Max v8ui environment — just what the
// hosts/max entry scripts touch. The mgraphics surface itself is typed where
// it is consumed (render/mgraphics.ts).

declare const mgraphics: any;
declare function outlet(index: number, ...args: unknown[]): void;
declare function post(...args: unknown[]): void;
