/*
 * VNSnowKey KWin 6 Window Focus Tracker Script
 * Notifies org.vnsnowkey.Daemon over D-Bus immediately when the active window changes.
 */

function notifyVNSnowKey(window) {
    if (!window) {
        return;
    }
    var appClass = window.resourceClass ? window.resourceClass.toString() : "";
    var appName = window.resourceName ? window.resourceName.toString() : "";
    var caption = window.caption ? window.caption.toString() : "";
    var winId = window.internalId ? window.internalId.toString() : "";

    callDBus(
        "org.vnsnowkey.Daemon",
        "/org/vnsnowkey/Daemon",
        "org.vnsnowkey.Daemon",
        "NotifyWindowFocus",
        appClass,
        appName,
        caption,
        winId
    );
}

// KWin 6 API
if (typeof workspace.windowActivated !== "undefined") {
    workspace.windowActivated.connect(notifyVNSnowKey);
    if (workspace.activeWindow) {
        notifyVNSnowKey(workspace.activeWindow);
    }
} else if (typeof workspace.clientActivated !== "undefined") {
    // KWin 5 fallback
    workspace.clientActivated.connect(notifyVNSnowKey);
    if (workspace.activeClient) {
        notifyVNSnowKey(workspace.activeClient);
    }
}
