/// Drag-priority gating for bidirectional controls (UI.md: "last-writer-wins
/// with a short drag-priority window"). While the user is dragging — and for
/// holdMs after release — externally-set values (OSC, attribute changes,
/// automation) are ignored so the control does not fight the hand.
///
/// Timestamps are passed in by the host (milliseconds, any monotonic-enough
/// clock) so the logic is deterministic and testable.

export class ValueGate {
    private dragging = false;
    private releasedAt = -Infinity;

    constructor(private readonly holdMs: number = 250) {}

    beginDrag(): void {
        this.dragging = true;
    }

    endDrag(now: number): void {
        this.dragging = false;
        this.releasedAt = now;
    }

    isDragging(): boolean {
        return this.dragging;
    }

    /** May an external value update be applied right now? */
    acceptExternal(now: number): boolean {
        return !this.dragging && now - this.releasedAt >= this.holdMs;
    }
}
