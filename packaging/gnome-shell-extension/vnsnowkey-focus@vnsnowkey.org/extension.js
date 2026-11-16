import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

export default class VNSnowKeyFocusExtension extends Extension {
    enable() {
        this._trackedWin = null;
        this._winClassSignalId = 0;
        this._winTitleSignalId = 0;
        this._cursorChangedSignalId = 0;
        this._nameWatchId = 0;

        this._focusSignalId = global.display.connect('notify::focus-window', () => {
            this._reportFocus();
        });
        try {
            let tracker = null;
            if (global.backend && typeof global.backend.get_cursor_tracker === 'function') {
                tracker = global.backend.get_cursor_tracker();
            } else if (Meta.CursorTracker && typeof Meta.CursorTracker.get_for_display === 'function') {
                tracker = Meta.CursorTracker.get_for_display(global.display);
            }
            if (tracker && typeof tracker.get_pointer_visible === 'function') {
                this._cursorTracker = tracker;
                this._cursorSignalId = tracker.connect('visibility-changed', () => {
                    this._reportCursorVisible();
                });
                try {
                    this._cursorChangedSignalId = tracker.connect('cursor-changed', () => {
                        this._reportCursorVisible();
                    });
                } catch (_e) {
                    this._cursorChangedSignalId = 0;
                }
            }
        } catch (_e) {
            this._cursorTracker = null;
            this._cursorSignalId = 0;
            this._cursorChangedSignalId = 0;
        }
        try {
            this._nameWatchId = Gio.bus_watch_name(
                Gio.BusType.SESSION,
                'org.vnsnowkey.Daemon',
                Gio.BusNameWatcherFlags.NONE,
                () => {
                    this._reportFocus();
                },
                null
            );
        } catch (_e) {
            this._nameWatchId = 0;
        }
        this._reportFocus();
    }

    disable() {
        this._disconnectTrackedWindow();
        if (this._nameWatchId) {
            try {
                Gio.bus_unwatch_name(this._nameWatchId);
            } catch (_e) {
            }
            this._nameWatchId = 0;
        }
        if (this._focusSignalId) {
            global.display.disconnect(this._focusSignalId);
            this._focusSignalId = 0;
        }
        if (this._cursorTracker) {
            if (this._cursorSignalId) {
                try {
                    this._cursorTracker.disconnect(this._cursorSignalId);
                } catch (_e) {
                }
            }
            if (this._cursorChangedSignalId) {
                try {
                    this._cursorTracker.disconnect(this._cursorChangedSignalId);
                } catch (_e) {
                }
            }
        }
        this._cursorTracker = null;
        this._cursorSignalId = 0;
        this._cursorChangedSignalId = 0;
    }

    _disconnectTrackedWindow() {
        if (this._trackedWin) {
            if (this._winClassSignalId) {
                try {
                    this._trackedWin.disconnect(this._winClassSignalId);
                } catch (_e) {
                }
            }
            if (this._winTitleSignalId) {
                try {
                    this._trackedWin.disconnect(this._winTitleSignalId);
                } catch (_e) {
                }
            }
        }
        this._trackedWin = null;
        this._winClassSignalId = 0;
        this._winTitleSignalId = 0;
    }

    _connectTrackedWindow(win) {
        if (this._trackedWin === win) {
            return;
        }
        this._disconnectTrackedWindow();
        if (!win) {
            return;
        }
        this._trackedWin = win;
        try {
            this._winClassSignalId = win.connect('notify::wm-class', () => {
                this._reportFocus();
            });
        } catch (_e) {
            this._winClassSignalId = 0;
        }
        try {
            this._winTitleSignalId = win.connect('notify::title', () => {
                this._reportFocus();
            });
        } catch (_e) {
            this._winTitleSignalId = 0;
        }
    }

    _reportCursorVisible() {
        if (!this._cursorTracker || typeof this._cursorTracker.get_pointer_visible !== 'function') {
            return;
        }
        const pointerVisible = Boolean(this._cursorTracker.get_pointer_visible());
        let hasSprite = true;
        try {
            if (typeof this._cursorTracker.get_sprite === 'function') {
                hasSprite = this._cursorTracker.get_sprite() !== null;
            }
        } catch (_e) {
            hasSprite = true;
        }
        const visible = pointerVisible && hasSprite;
        Gio.DBus.session.call(
            'org.vnsnowkey.Daemon',
            '/org/vnsnowkey/Daemon',
            'org.vnsnowkey.Daemon',
            'NotifyDesktopCursorVisible',
            new GLib.Variant('(b)', [visible]),
            null,
            Gio.DBusCallFlags.NO_AUTO_START,
            200,
            null,
            null
        );
    }

    _reportFocus() {
        const win = global.display.focus_window;
        this._connectTrackedWindow(win);
        if (!win) {
            return;
        }
        const gtkId = (typeof win.get_gtk_application_id === 'function') ? (win.get_gtk_application_id() || '') : '';
        const sandboxId = (typeof win.get_sandboxed_app_id === 'function') ? (win.get_sandboxed_app_id() || '') : '';
        const rawClass = win.get_wm_class() || '';
        const wmClass = rawClass || gtkId || sandboxId || '';
        let wmInstance = win.get_wm_class_instance() || gtkId || sandboxId || wmClass;
        const caption = win.get_title() || '';
        const winId = String(win.get_id() || '');

        const lowCls = `${wmClass} ${wmInstance}`.toLowerCase();
        if (!lowCls || lowCls.includes('java') || lowCls.includes('glfw') || lowCls.includes('lwjgl') ||
            lowCls.includes('prismlauncher') || lowCls.includes('modrinth') ||
            lowCls.includes('openjdk') || lowCls.includes('mojang')) {
            const pid = (typeof win.get_pid === 'function') ? win.get_pid() : 0;
            if (pid > 0) {
                try {
                    const [ok, bytes] = GLib.file_get_contents(`/proc/${pid}/cmdline`);
                    if (ok && bytes) {
                        const cmd = new TextDecoder().decode(bytes).replace(/\0/g, ' ').toLowerCase();
                        if (cmd.includes('net.minecraft') || cmd.includes('net.fabricmc') ||
                            cmd.includes('net.neoforged') || cmd.includes('com.mojang') ||
                            (cmd.includes('lwjgl') && cmd.includes('gamedir'))) {
                            wmInstance = 'minecraft';
                        }
                    }
                } catch (_e) {
                }
            }
        }

        Gio.DBus.session.call(
            'org.vnsnowkey.Daemon',
            '/org/vnsnowkey/Daemon',
            'org.vnsnowkey.Daemon',
            'NotifyWindowFocus',
            new GLib.Variant('(ssss)', [wmClass, wmInstance, caption, winId]),
            null,
            Gio.DBusCallFlags.NO_AUTO_START,
            200,
            null,
            null
        );
        this._reportCursorVisible();
    }
}
