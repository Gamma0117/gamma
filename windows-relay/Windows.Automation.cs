using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Automation;
using System.Windows.Forms;

namespace AuroraReviewRelay {
    public sealed class Binding {
        public long Handle { get; set; }
        public string Title { get; set; }
        public string ClassName { get; set; }
        public string ProcessName { get; set; }
        public string Executable { get; set; }
        public double XRatio { get; set; }
        public int BottomOffset { get; set; }
        public int Dpi { get; set; }
        public bool CoordinateOnly { get; set; }
        public bool CtrlEnter { get; set; }
    }

    public sealed class Prepared {
        public IntPtr Handle;
        public Binding Binding;
        public AutomationElement Editor;
        public IntPtr PreviousForeground;
    }

    public sealed class RelayForm : Form {
        public event EventHandler ToggleRequested;
        public bool HotkeyAvailable { get; private set; }
        protected override void OnHandleCreated(EventArgs e) {
            base.OnHandleCreated(e);
            HotkeyAvailable = Native.RegisterHotKey(Handle, 1906, 0x0001 | 0x0002 | 0x4000, 0x77);
        }
        protected override void OnHandleDestroyed(EventArgs e) {
            Native.UnregisterHotKey(Handle, 1906);
            base.OnHandleDestroyed(e);
        }
        protected override void WndProc(ref Message m) {
            if (m.Msg == 0x0312 && m.WParam.ToInt32() == 1906 && ToggleRequested != null)
                ToggleRequested(this, EventArgs.Empty);
            base.WndProc(ref m);
        }
    }

    public static class Native {
        [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
        [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] struct LastInput { public uint Size, Time; }
        [StructLayout(LayoutKind.Sequential)] struct MouseInput {
            public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra;
        }
        [StructLayout(LayoutKind.Sequential)] struct KeyboardInput {
            public ushort Key, Scan; public uint Flags, Time; public UIntPtr Extra;
        }
        [StructLayout(LayoutKind.Sequential)] struct HardwareInput { public uint Message; public ushort Low, High; }
        [StructLayout(LayoutKind.Explicit)] struct InputUnion {
            [FieldOffset(0)] public MouseInput Mouse;
            [FieldOffset(0)] public KeyboardInput Keyboard;
            [FieldOffset(0)] public HardwareInput Hardware;
        }
        [StructLayout(LayoutKind.Sequential)] struct Input { public uint Type; public InputUnion Data; }
        delegate bool WindowVisitor(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")] static extern bool GetCursorPos(out Point point);
        [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
        [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr window, uint flag);
        [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr window, ref Point point);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder text, int count);
        [DllImport("user32.dll")] static extern bool IsWindow(IntPtr window);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")] static extern bool EnumWindows(WindowVisitor visitor, IntPtr parameter);
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr window);
        [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr window);
        [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window, int command);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
        [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
        [DllImport("user32.dll")] static extern bool AttachThreadInput(uint first, uint second, bool attach);
        [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LastInput input);
        [DllImport("user32.dll")] static extern short GetAsyncKeyState(int key);
        [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint count, Input[] input, int size);
        [DllImport("user32.dll")] internal static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);
        [DllImport("user32.dll")] internal static extern bool UnregisterHotKey(IntPtr window, int id);
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr context);
        [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
        [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
        [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr obj, int index, StringBuilder info, uint length, out uint needed);

        public static void EnableDpiAwareness() {
            try { if (SetProcessDpiAwarenessContext(new IntPtr(-4))) return; } catch (EntryPointNotFoundException) { }
            SetProcessDPIAware();
        }
        static int Dpi(IntPtr window) {
            try { uint value = GetDpiForWindow(window); return value == 0 ? 96 : (int)value; }
            catch (EntryPointNotFoundException) { return 96; }
        }
        static string Title(IntPtr window) {
            StringBuilder text = new StringBuilder(2048); GetWindowText(window, text, text.Capacity); return text.ToString();
        }
        static string ClassName(IntPtr window) {
            StringBuilder text = new StringBuilder(256); GetClassName(window, text, text.Capacity); return text.ToString();
        }
        static string[] ProcessIdentity(IntPtr window) {
            uint id; GetWindowThreadProcessId(window, out id);
            using (Process process = Process.GetProcessById((int)id)) {
                string executable = "";
                try { executable = process.MainModule.FileName; } catch { }
                return new string[] { process.ProcessName, executable };
            }
        }
        static bool Matches(IntPtr window, Binding binding) {
            if (!IsWindow(window) || !IsWindowVisible(window) || Title(window) != binding.Title || ClassName(window) != binding.ClassName) return false;
            try {
                string[] process = ProcessIdentity(window);
                return String.Equals(process[0], binding.ProcessName, StringComparison.OrdinalIgnoreCase) &&
                    (String.IsNullOrEmpty(binding.Executable) || String.Equals(process[1], binding.Executable, StringComparison.OrdinalIgnoreCase));
            } catch { return false; }
        }
        static IntPtr Resolve(Binding binding) {
            IntPtr stored = new IntPtr(binding.Handle);
            if (Matches(stored, binding)) return stored;
            List<IntPtr> matches = new List<IntPtr>();
            EnumWindows(delegate(IntPtr window, IntPtr parameter) { if (Matches(window, binding)) matches.Add(window); return true; }, IntPtr.Zero);
            if (matches.Count != 1) throw new InvalidOperationException("Bound window is missing or ambiguous. Reconnect the composer.");
            return matches[0];
        }
        static bool DesktopReady() {
            IntPtr desktop = OpenInputDesktop(0, false, 1);
            if (desktop == IntPtr.Zero) return false;
            try {
                StringBuilder name = new StringBuilder(256); uint needed;
                return GetUserObjectInformation(desktop, 2, name, 512, out needed) &&
                    String.Equals(name.ToString(), "Default", StringComparison.OrdinalIgnoreCase);
            } finally { CloseDesktop(desktop); }
        }
        public static uint IdleMilliseconds() {
            LastInput input = new LastInput(); input.Size = (uint)Marshal.SizeOf(typeof(LastInput));
            if (!GetLastInputInfo(ref input)) return 0;
            return unchecked((uint)Environment.TickCount - input.Time);
        }
        static bool ModifiersHeld() {
            foreach (int key in new int[] { 0x10, 0x11, 0x12, 0x5B, 0x5C })
                if ((GetAsyncKeyState(key) & 0x8000) != 0) return true;
            return false;
        }
        static AutomationElement FindEditor(Point point) {
            AutomationElement element = AutomationElement.FromPoint(new System.Windows.Point(point.X, point.Y));
            for (int i = 0; i < 16 && element != null; i++, element = TreeWalker.RawViewWalker.GetParent(element)) {
                if (element.Current.ControlType == ControlType.Edit && element.Current.IsKeyboardFocusable && element.Current.IsEnabled)
                    return element;
            }
            return null;
        }
        static string EditorText(AutomationElement editor, bool requireWritable = false) {
            if (editor == null) throw new InvalidOperationException("Composer is not exposed as an editable UI Automation control.");
            object pattern;
            if (editor.TryGetCurrentPattern(ValuePattern.Pattern, out pattern)) {
                ValuePattern value = (ValuePattern)pattern;
                if (requireWritable && value.Current.IsReadOnly) throw new InvalidOperationException("Composer is read-only.");
                return value.Current.Value;
            }
            if (editor.TryGetCurrentPattern(TextPattern.Pattern, out pattern))
                return ((TextPattern)pattern).DocumentRange.GetText(8192);
            throw new InvalidOperationException("Composer text cannot be verified.");
        }
        static string Normalize(string text) { return text.Replace("\r\n", "\n").Replace("\r", "\n").Trim(); }
        static Point TargetPoint(IntPtr window, Binding binding) {
            Rect rect; Point origin = new Point();
            if (!GetClientRect(window, out rect) || !ClientToScreen(window, ref origin)) throw new InvalidOperationException("Cannot read the window rectangle.");
            int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
            int y = height - (int)Math.Round(binding.BottomOffset * (double)Dpi(window) / binding.Dpi);
            int x = (int)Math.Round(width * binding.XRatio);
            if (width < 350 || height < 250 || x < 2 || x >= width - 2 || y < 2 || y >= height - 2)
                throw new InvalidOperationException("Window layout changed. Reconnect the composer.");
            return new Point { X = origin.X + x, Y = origin.Y + y };
        }
        static void Foreground(IntPtr window) {
            ShowWindow(window, 9);
            if (GetForegroundWindow() == window) return;
            SetForegroundWindow(window);
            if (GetForegroundWindow() != window) {
                uint unused; uint foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), out unused);
                uint currentThread = GetCurrentThreadId();
                bool attached = foregroundThread != 0 && foregroundThread != currentThread && AttachThreadInput(currentThread, foregroundThread, true);
                try { BringWindowToTop(window); SetForegroundWindow(window); }
                finally { if (attached) AttachThreadInput(currentThread, foregroundThread, false); }
            }
            Thread.Sleep(150);
            if (GetForegroundWindow() != window) throw new InvalidOperationException("Windows refused foreground activation. No text was sent.");
        }
        static void MouseClick(Point point) {
            SetCursorPos(point.X, point.Y);
            Input down = new Input { Type = 0 }; down.Data.Mouse.Flags = 0x0002;
            Input up = new Input { Type = 0 }; up.Data.Mouse.Flags = 0x0004;
            Send(new Input[] { down, up });
        }
        static Input Key(ushort key, ushort scan, uint flags) {
            Input input = new Input { Type = 1 }; input.Data.Keyboard.Key = key;
            input.Data.Keyboard.Scan = scan; input.Data.Keyboard.Flags = flags; return input;
        }
        static void Send(Input[] input) {
            if (SendInput((uint)input.Length, input, Marshal.SizeOf(typeof(Input))) != input.Length)
                throw new InvalidOperationException("Windows refused keyboard/mouse input. Do not elevate automatically.");
        }
        public static Binding Capture(bool coordinateOnly, bool ctrlEnter, IntPtr relayWindow) {
            Point point; if (!GetCursorPos(out point)) throw new InvalidOperationException("Cannot read the mouse position.");
            IntPtr window = GetAncestor(WindowFromPoint(point), 2);
            if (window == IntPtr.Zero || window == relayWindow) throw new InvalidOperationException("Hover over the target app composer, not this relay window.");
            string title = Title(window); if (String.IsNullOrWhiteSpace(title)) throw new InvalidOperationException("The window has no stable title.");
            Rect rect; Point origin = new Point(); GetClientRect(window, out rect); ClientToScreen(window, ref origin);
            if (rect.Right < 350 || rect.Bottom < 250) throw new InvalidOperationException("Restore the target window before connecting it.");
            if (!coordinateOnly) {
                AutomationElement editor = FindEditor(point);
                if (Normalize(EditorText(editor, true)).Length != 0) throw new InvalidOperationException("Composer contains a draft. Clear it before connecting.");
            }
            string[] process = ProcessIdentity(window);
            return new Binding { Handle = window.ToInt64(), Title = title, ClassName = ClassName(window), ProcessName = process[0], Executable = process[1],
                XRatio = (point.X - origin.X) / (double)rect.Right, BottomOffset = rect.Bottom - (point.Y - origin.Y),
                Dpi = Dpi(window), CoordinateOnly = coordinateOnly, CtrlEnter = ctrlEnter };
        }
        public static Prepared Prepare(Binding binding) {
            if (!DesktopReady()) throw new InvalidOperationException("Unlock the Windows desktop before delivery.");
            if (ModifiersHeld()) throw new InvalidOperationException("Release Shift/Ctrl/Alt/Windows keys before delivery.");
            IntPtr previous = GetForegroundWindow(); IntPtr window = Resolve(binding); Foreground(window);
            Point point = TargetPoint(window, binding); AutomationElement editor = null;
            if (binding.CoordinateOnly) { MouseClick(point); Thread.Sleep(120); }
            else {
                editor = FindEditor(point);
                if (Normalize(EditorText(editor, true)).Length != 0) throw new InvalidOperationException("Composer contains a draft. No text was changed.");
                editor.SetFocus(); Thread.Sleep(100);
                if (!editor.Current.HasKeyboardFocus) throw new InvalidOperationException("Cannot verify composer focus.");
            }
            if (GetForegroundWindow() != window) throw new InvalidOperationException("Foreground changed during preparation.");
            return new Prepared { Handle = window, Binding = binding, Editor = editor, PreviousForeground = previous };
        }
        public static string Deliver(Prepared prepared, string prompt) {
            Binding binding = prepared.Binding;
            if (!DesktopReady() || !Matches(prepared.Handle, binding) || GetForegroundWindow() != prepared.Handle || ModifiersHeld())
                throw new InvalidOperationException("Foreground or keyboard state changed; delivery is uncertain.");
            if (!binding.CoordinateOnly && (!prepared.Editor.Current.IsEnabled || !prepared.Editor.Current.HasKeyboardFocus ||
                Normalize(EditorText(prepared.Editor, true)).Length != 0))
                throw new InvalidOperationException("Composer changed before typing. No Enter key was sent.");
            // Unicode keyboard packets avoid changing the user's clipboard. Keep this a single line.
            string text = prompt.Replace("\r\n", " ").Replace("\r", " ").Replace("\n", " ");
            List<Input> characters = new List<Input>();
            foreach (char character in text) { characters.Add(Key(0, character, 0x0004)); characters.Add(Key(0, character, 0x0004 | 0x0002)); }
            Send(characters.ToArray());
            bool exact = binding.CoordinateOnly;
            if (!binding.CoordinateOnly) {
                for (int i = 0; i < 20; i++) {
                    Thread.Sleep(100);
                    if (Normalize(EditorText(prepared.Editor)) == Normalize(text)) { exact = true; break; }
                }
            }
            if (!exact || !DesktopReady() || GetForegroundWindow() != prepared.Handle || ModifiersHeld() ||
                (!binding.CoordinateOnly && !prepared.Editor.Current.HasKeyboardFocus))
                throw new InvalidOperationException("Typed text or composer focus changed. Do not retry automatically.");
            try {
                if (binding.CtrlEnter) Send(new Input[] { Key(0x11, 0, 0), Key(0x0D, 0, 0), Key(0x0D, 0, 2), Key(0x11, 0, 2) });
                else Send(new Input[] { Key(0x0D, 0, 0), Key(0x0D, 0, 2) });
            } finally {
                // Release only keys introduced by this relay, including a partial SendInput failure.
                if (binding.CtrlEnter) SendInput(2, new Input[] { Key(0x0D, 0, 2), Key(0x11, 0, 2) }, Marshal.SizeOf(typeof(Input)));
            }
            if (!binding.CoordinateOnly) {
                for (int i = 0; i < 20; i++) {
                    Thread.Sleep(100);
                    if (Normalize(EditorText(prepared.Editor)).Length == 0) {
                        if (prepared.PreviousForeground != IntPtr.Zero && IsWindow(prepared.PreviousForeground)) SetForegroundWindow(prepared.PreviousForeground);
                        return "verified";
                    }
                }
                throw new InvalidOperationException("Enter was sent but the composer did not clear. Verify the app before retrying.");
            }
            return "coordinate-attempt";
        }
    }
}
