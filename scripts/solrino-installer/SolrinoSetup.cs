// Solrino's installer: a single program holding the app as an embedded zip.
// It draws its whole window itself on a per-pixel-alpha surface, which is what
// allows the soft shadow, rounded corners and glow.
//
// Compiled by build-installer.ps1 with the C# compiler included in Windows.
// Test switches: --dest <folder> --no-register --no-launch --auto
// --profile-root <folder> --local-root <folder> (use these instead of AppData)
// The app's updater runs it with --update: it waits for the app to close, then
// installs with no questions and keeps the user's shortcut choice.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace SolrinoSetup
{
    enum Screen { Import, Welcome, Installing, Done, Problem }

    class Options
    {
        public string Destination = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Programs\Solrino");
        public bool Register = true;
        public bool Launch = true;
        public bool Auto = false;
        public bool Update = false;
        public string ProfileRoot = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        /// Bluzyrino and others keep their data here rather than in the folder above
        public string LocalRoot = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        /// null until the import question is answered. Empty means a fresh start.
        public string ImportAnswer = null;

        public static Options Parse(string[] args)
        {
            var o = new Options();
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "--dest" && i + 1 < args.Length) { o.Destination = args[++i]; }
                else if (args[i] == "--no-register") { o.Register = false; }
                else if (args[i] == "--no-launch") { o.Launch = false; }
                else if (args[i] == "--auto") { o.Auto = true; }
                else if (args[i] == "--profile-root" && i + 1 < args.Length) { o.ProfileRoot = args[++i]; }
                else if (args[i] == "--local-root" && i + 1 < args.Length) { o.LocalRoot = args[++i]; }
                else if (args[i] == "--update") { o.Auto = true; o.Update = true; }
            }
            return o;
        }
    }

    /// A profile from Chatterino or one of its forks.
    class ForkProfile
    {
        public string Folder;
        public string Name;
        public string LastUsed;

        const string SolrinoFolder = "Solrino";

        static bool LooksLikeProfile(string folder)
        {
            return File.Exists(Path.Combine(folder, @"Settings\settings.json"))
                && File.Exists(Path.Combine(folder, @"Settings\window-layout.json"));
        }

        public static bool IsFirstInstall(Options options)
        {
            return !File.Exists(Path.Combine(options.ProfileRoot, SolrinoFolder, @"Settings\settings.json"));
        }

        public static ForkProfile FromFolder(string folder)
        {
            if (!LooksLikeProfile(folder)) { return null; }
            var profile = new ForkProfile();
            profile.Folder = folder;
            string name = Path.GetFileName(folder.TrimEnd('\\'));
            // Moltorino and most other forks share Chatterino's own folder
            profile.Name = name.Equals("Chatterino2", StringComparison.OrdinalIgnoreCase) ? "Moltorino / Chatterino" : name;
            var used = File.GetLastWriteTime(Path.Combine(folder, @"Settings\settings.json"));
            profile.LastUsed = "Last used " + used.ToString("MMM d, yyyy");
            return profile;
        }

        /// Any folder set out like Chatterino's, so forks under other names are found.
        public static List<ForkProfile> Find(Options options)
        {
            var found = new List<ForkProfile>();
            var folders = new List<string>();
            foreach (var root in new[] { options.ProfileRoot, options.LocalRoot })
            {
                try { folders.AddRange(Directory.GetDirectories(root)); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }

            foreach (var folder in folders)
            {
                if (Path.GetFileName(folder).Equals(SolrinoFolder, StringComparison.OrdinalIgnoreCase)) { continue; }
                try
                {
                    var profile = FromFolder(folder);
                    if (profile != null) { found.Add(profile); }
                }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
            found.Sort(delegate (ForkProfile a, ForkProfile b)
            {
                return File.GetLastWriteTime(Path.Combine(b.Folder, @"Settings\settings.json"))
                    .CompareTo(File.GetLastWriteTime(Path.Combine(a.Folder, @"Settings\settings.json")));
            });
            return found;
        }
    }

    /// Does the actual install on a background thread.
    class Installer
    {
        readonly Options options;
        readonly bool desktopShortcut;
        public volatile string CurrentFile = "";
        public volatile bool Finished;
        public volatile string Error;
        long done;
        long total = 1;

        public double Progress { get { return Math.Min(1.0, (double)Interlocked.Read(ref done) / Interlocked.Read(ref total)); } }

        public Installer(Options options, bool desktopShortcut)
        {
            this.options = options;
            this.desktopShortcut = desktopShortcut;
        }

        public void Start()
        {
            var thread = new Thread(Run);
            thread.IsBackground = true;
            thread.Start();
        }

        void Run()
        {
            try
            {
                Install();
            }
            catch (IOException e) { Error = "A file couldn't be written. Close Solrino and try again.\n" + e.Message; }
            catch (UnauthorizedAccessException) { Error = "Windows blocked access to the install folder. Close Solrino and try again."; }
            catch (Exception e) { Error = e.Message; }
            Finished = true;
        }

        void Install()
        {
            var assembly = Assembly.GetExecutingAssembly();
            string root = Path.GetFullPath(options.Destination);
            string rootPrefix = root.TrimEnd('\\') + "\\";
            Directory.CreateDirectory(root);

            using (var stream = assembly.GetManifestResourceStream("app.zip"))
            {
                if (stream == null) { throw new Exception("This setup file is damaged. Please download it again."); }
                using (var zip = new ZipArchive(stream, ZipArchiveMode.Read))
                {
                    long sum = 1;
                    foreach (var entry in zip.Entries) { sum += entry.Length; }
                    Interlocked.Exchange(ref total, sum);

                    var buffer = new byte[256 * 1024];
                    foreach (var entry in zip.Entries)
                    {
                        string relative = entry.FullName.Replace('/', '\\');
                        string path = Path.GetFullPath(Path.Combine(root, relative));
                        if (!path.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase))
                        {
                            throw new Exception("This setup file is damaged. Please download it again.");
                        }
                        if (relative.EndsWith("\\")) { Directory.CreateDirectory(path); continue; }

                        CurrentFile = relative;
                        Directory.CreateDirectory(Path.GetDirectoryName(path));
                        using (var input = entry.Open())
                        using (var output = File.Create(path))
                        {
                            int read;
                            while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                            {
                                output.Write(buffer, 0, read);
                                Interlocked.Add(ref done, read);
                            }
                        }
                    }
                }
            }

            // Left by older test builds
            string old = Path.Combine(root, "Emberino.exe");
            if (File.Exists(old)) { try { File.Delete(old); } catch (IOException) { } }

            using (var stream = assembly.GetManifestResourceStream("uninstall.ps1"))
            using (var output = File.Create(Path.Combine(root, "uninstall.ps1")))
            {
                stream.CopyTo(output);
            }

            string exe = Path.Combine(root, "Solrino.exe");
            if (!File.Exists(exe)) { throw new Exception("Solrino.exe wasn't installed. Please download the setup again."); }

            // The app acts on this when it first opens
            if (options.ImportAnswer != null)
            {
                string profile = Path.Combine(options.ProfileRoot, "Solrino");
                Directory.CreateDirectory(profile);
                File.WriteAllText(Path.Combine(profile, "first-run-import.txt"), options.ImportAnswer);
            }

            if (options.Register)
            {
                CurrentFile = "Creating shortcuts";
                MakeShortcut(Environment.GetFolderPath(Environment.SpecialFolder.Programs), exe, root);
                if (desktopShortcut)
                {
                    MakeShortcut(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), exe, root);
                }
                Register(exe, root);
            }
            CurrentFile = "";
        }

        static void MakeShortcut(string folder, string exe, string workingDirectory)
        {
            dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
            dynamic link = shell.CreateShortcut(Path.Combine(folder, "Solrino.lnk"));
            link.TargetPath = exe;
            link.WorkingDirectory = workingDirectory;
            link.IconLocation = exe + ",0";
            link.Description = "Solrino chat client";
            link.Save();
        }

        static void Register(string exe, string root)
        {
            using (var key = Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\Solrino"))
            {
                key.SetValue("DisplayName", "Solrino");
                key.SetValue("DisplayIcon", exe + ",0");
                key.SetValue("DisplayVersion", FileVersionInfo.GetVersionInfo(exe).ProductVersion ?? "");
                key.SetValue("InstallLocation", root);
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                key.SetValue("UninstallString",
                    "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + root + "\\uninstall.ps1\"");
            }
        }
    }

    class Ember
    {
        public float X, Y, Speed, Size, Drift, Phase;
    }

    class Hotspot
    {
        public RectangleF Rect;
        public Action Click;
    }

    class SetupWindow : Form
    {
        // Logical size. Everything is drawn in these units and scaled for the display.
        const int ShadowSize = 26;
        const int CardW = 520, CardH = 340;
        const int WinW = CardW + ShadowSize * 2, WinH = CardH + ShadowSize * 2;
        const float Radius = 16f;

        static readonly Color Ink = Color.FromArgb(14, 8, 9);
        static readonly Color InkLight = Color.FromArgb(30, 13, 15);
        static readonly Color Red = Color.FromArgb(227, 16, 28);
        static readonly Color RedBright = Color.FromArgb(255, 72, 72);
        static readonly Color RedDeep = Color.FromArgb(126, 8, 18);
        static readonly Color Text1 = Color.FromArgb(246, 238, 236);
        static readonly Color Text2 = Color.FromArgb(172, 150, 148);

        readonly Options options;
        readonly float scale;
        readonly Bitmap logo;
        readonly List<Ember> embers = new List<Ember>();
        readonly Random random = new Random(7);
        readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
        readonly Stopwatch clock = Stopwatch.StartNew();
        readonly List<Hotspot> hotspots = new List<Hotspot>();

        Screen screen = Screen.Welcome;
        List<ForkProfile> forks = new List<ForkProfile>();
        int chosenFork;
        Installer installer;
        bool desktopShortcut = true;
        string problem = "";
        double shownProgress;
        double screenStart;
        double doneAt = -1;
        double fadeOutStart = -1;
        double last;
        PointF mouse = new PointF(-1, -1);
        bool mouseDown;
        Point dragOrigin;
        bool dragging;
        Font fontTitle, fontHeading, fontBody, fontSmall, fontButton, fontPercent;

        public SetupWindow(Options options)
        {
            this.options = options;
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.Manual;
            Text = "Solrino Setup";
            ShowInTaskbar = true;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (ArgumentException) { }

            using (var g = Graphics.FromHwnd(IntPtr.Zero)) { scale = g.DpiX / 96f; }
            Size = new Size((int)(WinW * scale), (int)(WinH * scale));
            var area = System.Windows.Forms.Screen.FromPoint(Cursor.Position).WorkingArea;
            Location = new Point(area.Left + (area.Width - Width) / 2, area.Top + (area.Height - Height) / 2);

            using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("logo.png"))
            {
                logo = new Bitmap(stream);
            }

            string family = "Segoe UI";
            fontTitle = new Font(family + " Semibold", 26f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontHeading = new Font(family + " Semibold", 19f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontBody = new Font(family, 13.5f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontSmall = new Font(family, 11.5f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontButton = new Font(family + " Semibold", 15f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontPercent = new Font(family + " Light", 44f, FontStyle.Regular, GraphicsUnit.Pixel);

            for (int i = 0; i < 34; i++) { embers.Add(NewEmber(true)); }

            // Only the first time, and only if there's something to bring over
            if (!options.Auto && ForkProfile.IsFirstInstall(options))
            {
                forks = ForkProfile.Find(options);
                if (forks.Count > 3) { forks.RemoveRange(3, forks.Count - 3); }
                if (forks.Count > 0) { screen = Screen.Import; }
            }

            timer.Interval = 16;
            timer.Tick += delegate { Tick(); };
            Shown += delegate
            {
                timer.Start();
                if (options.Auto) { BeginInstall(); }
            };
        }

        protected override CreateParams CreateParams
        {
            get
            {
                var cp = base.CreateParams;
                cp.ExStyle |= 0x00080000; // WS_EX_LAYERED
                return cp;
            }
        }

        double Now { get { return clock.Elapsed.TotalSeconds; } }

        Ember NewEmber(bool anywhere)
        {
            var e = new Ember();
            e.X = (float)random.NextDouble() * CardW;
            e.Y = anywhere ? (float)random.NextDouble() * CardH : CardH + 6;
            e.Speed = 7f + (float)random.NextDouble() * 20f;
            e.Size = 1f + (float)random.NextDouble() * 2.4f;
            e.Drift = 5f + (float)random.NextDouble() * 12f;
            e.Phase = (float)random.NextDouble() * 6.28f;
            return e;
        }

        void Go(Screen next)
        {
            screen = next;
            screenStart = Now;
        }

        void BeginInstall()
        {
            // The app is closing itself to update: give it time to finish
            if (options.Update)
            {
                desktopShortcut = File.Exists(Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Solrino.lnk"));
                for (int waited = 0; waited < 60 && IsRunningFromDestination(); waited++)
                {
                    Thread.Sleep(500);
                }
            }

            if (IsRunningFromDestination())
            {
                problem = "Solrino is still open. Right-click its icon near the clock, choose Quit, then try again.";
                Go(Screen.Problem);
                return;
            }
            shownProgress = 0;
            doneAt = -1;
            installer = new Installer(options, desktopShortcut);
            installer.Start();
            Go(Screen.Installing);
        }

        bool IsRunningFromDestination()
        {
            string root = Path.GetFullPath(options.Destination).TrimEnd('\\') + "\\";
            foreach (var p in Process.GetProcessesByName("Solrino"))
            {
                try
                {
                    if (p.MainModule.FileName.StartsWith(root, StringComparison.OrdinalIgnoreCase)) { return true; }
                }
                catch (System.ComponentModel.Win32Exception) { }
                catch (InvalidOperationException) { }
            }
            return false;
        }

        void Tick()
        {
            double now = Now;
            float dt = (float)Math.Min(0.05, now - last);
            last = now;

            foreach (var e in embers) { e.Y -= e.Speed * dt; }
            for (int i = 0; i < embers.Count; i++)
            {
                if (embers[i].Y < -6) { embers[i] = NewEmber(false); }
            }

            if (screen == Screen.Installing && installer != null)
            {
                // Eases toward the real number so the bar moves smoothly
                // and never races through too fast to see on a quick PC
                double step = (installer.Progress - shownProgress) * Math.Min(1.0, dt * 9.0);
                shownProgress += Math.Min(step, dt / 1.5);
                if (installer.Finished && shownProgress > 0.985 || installer.Error != null && installer.Finished)
                {
                    if (installer.Error != null)
                    {
                        problem = installer.Error;
                        Go(Screen.Problem);
                    }
                    else
                    {
                        shownProgress = 1;
                        doneAt = now;
                        Go(Screen.Done);
                    }
                }
            }

            if (screen == Screen.Done && fadeOutStart < 0 && now - doneAt > 1.5)
            {
                fadeOutStart = now;
                if (options.Launch) { LaunchApp(); }
            }
            if (fadeOutStart >= 0 && now - fadeOutStart > 0.35)
            {
                timer.Stop();
                Close();
                return;
            }

            Render();
        }

        void LaunchApp()
        {
            try
            {
                var start = new ProcessStartInfo(Path.Combine(options.Destination, "Solrino.exe"));
                start.WorkingDirectory = options.Destination;
                start.UseShellExecute = true;
                Process.Start(start);
            }
            catch (System.ComponentModel.Win32Exception)
            {
                // Installed fine: the desktop or Start menu shortcut still opens it
            }
        }

        // ------------------------------------------------------------ Drawing

        static GraphicsPath Rounded(RectangleF r, float radius)
        {
            var path = new GraphicsPath();
            float d = radius * 2;
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        static double EaseOut(double t)
        {
            t = Math.Max(0, Math.Min(1, t));
            return 1 - Math.Pow(1 - t, 3);
        }

        static Color Fade(Color c, double alpha)
        {
            return Color.FromArgb((int)(Math.Max(0, Math.Min(1, alpha)) * c.A), c);
        }

        void Render()
        {
            int w = Width, h = Height;
            using (var surface = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(surface))
                {
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    g.ScaleTransform(scale, scale);
                    Draw(g);
                }

                double intro = EaseOut(Now / 0.35);
                double outro = fadeOutStart >= 0 ? 1 - (Now - fadeOutStart) / 0.35 : 1;
                Present(surface, Math.Max(0, Math.Min(intro, outro)));
            }
        }

        void Draw(Graphics g)
        {
            hotspots.Clear();
            DrawShadow(g);

            g.TranslateTransform(ShadowSize, ShadowSize);
            var card = new RectangleF(0, 0, CardW, CardH);
            using (var path = Rounded(card, Radius))
            {
                using (var fill = new LinearGradientBrush(card, InkLight, Ink, 90f))
                {
                    g.FillPath(fill, path);
                }
                var state = g.Save();
                g.SetClip(path);
                DrawGlow(g);
                DrawEmbers(g);
                g.Restore(state);
                using (var pen = new Pen(Color.FromArgb(70, 170, 30, 40), 1f))
                {
                    g.DrawPath(pen, path);
                }
            }

            switch (screen)
            {
                case Screen.Import: DrawImport(g); break;
                case Screen.Welcome: DrawWelcome(g); break;
                case Screen.Installing: DrawInstalling(g); break;
                case Screen.Done: DrawDone(g); break;
                case Screen.Problem: DrawProblem(g); break;
            }

            if (screen != Screen.Installing && screen != Screen.Done) { DrawClose(g); }
            g.TranslateTransform(-ShadowSize, -ShadowSize);
        }

        void DrawShadow(Graphics g)
        {
            for (int i = ShadowSize; i > 0; i -= 2)
            {
                var r = new RectangleF(ShadowSize - i, ShadowSize - i + 4, CardW + i * 2, CardH + i * 2 - 4);
                using (var path = Rounded(r, Radius + i))
                using (var brush = new SolidBrush(Color.FromArgb(5, 0, 0, 0)))
                {
                    g.FillPath(brush, path);
                }
            }
        }

        void DrawGlow(Graphics g)
        {
            double pulse = 0.5 + 0.5 * Math.Sin(Now * 1.3);
            float cx = CardW / 2f, cy = 92f, rx = 280f, ry = 170f;
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
                using (var brush = new PathGradientBrush(path))
                {
                    brush.CenterColor = Color.FromArgb((int)(62 + 26 * pulse), 200, 18, 30);
                    brush.SurroundColors = new[] { Color.FromArgb(0, 200, 18, 30) };
                    g.FillPath(brush, path);
                }
            }
        }

        void DrawEmbers(Graphics g)
        {
            foreach (var e in embers)
            {
                float x = e.X + (float)Math.Sin(Now * 0.8 + e.Phase) * e.Drift;
                double life = Math.Max(0, Math.Min(1, e.Y / CardH));
                int alpha = (int)(40 + 150 * life * (0.6 + 0.4 * Math.Sin(Now * 2.2 + e.Phase)));
                using (var brush = new SolidBrush(Color.FromArgb(Math.Max(0, Math.Min(255, alpha)), 255, 70, 50)))
                {
                    g.FillEllipse(brush, x, e.Y, e.Size, e.Size);
                }
            }
        }

        /// Draws the logo with the same darker sweep as the app's title bar.
        void DrawLogo(Graphics g, float cx, float cy, float side, double alpha)
        {
            int px = Math.Max(8, (int)(side * scale));
            using (var layer = new Bitmap(px, px, System.Drawing.Imaging.PixelFormat.Format32bppPArgb))
            {
                using (var lg = Graphics.FromImage(layer))
                {
                    lg.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    lg.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    lg.SmoothingMode = SmoothingMode.AntiAlias;
                    lg.DrawImage(logo, 0, 0, px, px);
                }
                ApplySweep(layer, (Now % 2.4) / 2.4);

                using (var attributes = new ImageAttributes())
                {
                    var matrix = new ColorMatrix();
                    matrix.Matrix33 = (float)alpha;
                    attributes.SetColorMatrix(matrix);
                    g.DrawImage(layer,
                        new[]
                        {
                            new PointF(cx - side / 2, cy - side / 2),
                            new PointF(cx + side / 2, cy - side / 2),
                            new PointF(cx - side / 2, cy + side / 2)
                        },
                        new RectangleF(0, 0, px, px), GraphicsUnit.Pixel, attributes);
                }
            }
        }

        /// Darkens a moving diagonal band, only where the logo has pixels.
        static void ApplySweep(Bitmap bitmap, double progress)
        {
            var rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
            var data = bitmap.LockBits(rect, ImageLockMode.ReadWrite, System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
            try
            {
                int w = bitmap.Width, h = bitmap.Height;
                double center = -0.6 * w + progress * 2.2 * w;
                double band = 0.42 * w;
                var row = new byte[w * 4];
                for (int y = 0; y < h; y++)
                {
                    IntPtr line = new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride);
                    Marshal.Copy(line, row, 0, row.Length);
                    for (int x = 0; x < w; x++)
                    {
                        double distance = Math.Abs((x + y * 0.4) - center) / band;
                        if (distance >= 1) { continue; }
                        double keep = 1 - 0.74 * (1 - distance) * (1 - distance);
                        int i = x * 4;
                        row[i] = (byte)(row[i] * keep);
                        row[i + 1] = (byte)(row[i + 1] * keep);
                        row[i + 2] = (byte)(row[i + 2] * keep);
                    }
                    Marshal.Copy(row, 0, line, row.Length);
                }
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
        }

        void Centered(Graphics g, string text, Font font, Color color, float y)
        {
            using (var brush = new SolidBrush(color))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                g.DrawString(text, font, brush, new RectangleF(18, y, CardW - 36, 400), format);
            }
        }

        void AnswerImport(string folder)
        {
            options.ImportAnswer = folder;
            Go(Screen.Welcome);
        }

        void PickImportFolder()
        {
            using (var dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Choose the folder that holds the other app's Settings folder";
                dialog.ShowNewFolderButton = false;
                if (dialog.ShowDialog(this) != DialogResult.OK) { return; }
                ForkProfile profile = null;
                try { profile = ForkProfile.FromFolder(dialog.SelectedPath); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
                if (profile == null)
                {
                    MessageBox.Show(this, "That folder doesn't have settings and tabs in it. Choose the folder that holds a folder called Settings.", "Solrino Setup");
                    return;
                }
                if (forks.Count >= 3) { forks.RemoveAt(2); }
                forks.Insert(0, profile);
                chosenFork = 0;
            }
        }

        void DrawImport(Graphics g)
        {
            double t = Now - screenStart;
            double rise = EaseOut(t / 0.7);
            DrawLogo(g, CardW / 2f, 44 + (float)((1 - rise) * 30), 54, rise);
            double text = EaseOut((t - 0.15) / 0.45);
            Centered(g, "Import from another fork?", fontHeading, Fade(Text1, text), 72);
            using (var brush = new SolidBrush(Fade(Text2, text)))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                g.DrawString("Would you like to import your settings and chat tabs from another fork such as Moltorino or Bluzyrino?",
                    fontBody, brush, new RectangleF(60, 100, CardW - 120, 40), format);
            }

            double controls = EaseOut((t - 0.3) / 0.45);
            float rowHeight = 32f;
            float rowsTop = 146 + (3 - forks.Count) * rowHeight / 2;
            for (int i = 0; i < forks.Count; i++)
            {
                DrawForkRow(g, new RectangleF(70, rowsTop + i * rowHeight, CardW - 140, rowHeight - 4), i, controls);
            }

            DrawLink(g, "Choose another folder", 244, controls, delegate { PickImportFolder(); });
            using (var brush = new SolidBrush(Fade(Color.FromArgb(150, Text2), controls)))
            {
                var format = new StringFormat();
                format.Alignment = StringAlignment.Center;
                g.DrawString("Your logins come too. The other app isn't changed.", fontSmall, brush, new RectangleF(18, 314, CardW - 36, 20), format);
                format.Dispose();
            }

            DrawSecondaryButton(g, new RectangleF(CardW / 2f - 176, 266, 168, 42), "Start fresh", controls, delegate { AnswerImport(""); });
            DrawButton(g, new RectangleF(CardW / 2f + 8, 266, 168, 42), "Import", controls, delegate { AnswerImport(forks[chosenFork].Folder); });
        }

        void DrawForkRow(Graphics g, RectangleF r, int index, double alpha)
        {
            bool chosen = index == chosenFork;
            bool hover = r.Contains(mouse);
            using (var path = Rounded(r, 8))
            {
                if (chosen || hover)
                {
                    using (var brush = new SolidBrush(Fade(chosen ? Color.FromArgb(58, 227, 16, 28) : Color.FromArgb(22, 255, 255, 255), alpha)))
                    {
                        g.FillPath(brush, path);
                    }
                }
                using (var pen = new Pen(Fade(chosen ? Color.FromArgb(190, 255, 72, 72) : Color.FromArgb(46, 255, 255, 255), alpha), 1f))
                {
                    g.DrawPath(pen, path);
                }
            }

            var dot = new RectangleF(r.X + 10, r.Y + r.Height / 2 - 7, 14, 14);
            using (var pen = new Pen(Fade(chosen ? RedBright : Text2, alpha), 1.4f)) { g.DrawEllipse(pen, dot); }
            if (chosen)
            {
                using (var brush = new SolidBrush(Fade(RedBright, alpha)))
                {
                    g.FillEllipse(brush, dot.X + 3.5f, dot.Y + 3.5f, 7, 7);
                }
            }

            using (var format = new StringFormat())
            {
                format.LineAlignment = StringAlignment.Center;
                format.Trimming = StringTrimming.EllipsisCharacter;
                format.FormatFlags = StringFormatFlags.NoWrap;
                using (var brush = new SolidBrush(Fade(Text1, alpha)))
                {
                    g.DrawString(forks[index].Name, fontBody, brush, new RectangleF(r.X + 32, r.Y, r.Width - 160, r.Height), format);
                }
                format.Alignment = StringAlignment.Far;
                using (var brush = new SolidBrush(Fade(Text2, alpha)))
                {
                    g.DrawString(forks[index].LastUsed, fontSmall, brush, new RectangleF(r.Right - 138, r.Y, 128, r.Height), format);
                }
            }
            hotspots.Add(new Hotspot { Rect = r, Click = delegate { chosenFork = index; } });
        }

        void DrawLink(Graphics g, string label, float y, double alpha, Action click)
        {
            var size = g.MeasureString(label, fontSmall);
            var r = new RectangleF((CardW - size.Width) / 2, y, size.Width, size.Height);
            bool hover = r.Contains(mouse);
            using (var brush = new SolidBrush(Fade(hover ? Text1 : Text2, alpha)))
            {
                g.DrawString(label, fontSmall, brush, r.X, r.Y);
            }
            hotspots.Add(new Hotspot { Rect = r, Click = click });
        }

        void DrawSecondaryButton(Graphics g, RectangleF r, string label, double alpha, Action click)
        {
            bool hover = r.Contains(mouse);
            if (hover && mouseDown) { r.Inflate(-1.5f, -1); }
            using (var path = Rounded(r, 11))
            {
                using (var fill = new SolidBrush(Fade(Color.FromArgb(hover ? 34 : 16, 255, 255, 255), alpha))) { g.FillPath(fill, path); }
                using (var pen = new Pen(Fade(Color.FromArgb(hover ? 120 : 70, 255, 255, 255), alpha), 1f)) { g.DrawPath(pen, path); }
            }
            using (var brush = new SolidBrush(Fade(Text1, alpha)))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                format.LineAlignment = StringAlignment.Center;
                g.DrawString(label, fontButton, brush, r, format);
            }
            hotspots.Add(new Hotspot { Rect = r, Click = click });
        }

        void DrawWelcome(Graphics g)
        {
            double t = Now - screenStart;
            double rise = EaseOut(t / 0.8);
            DrawLogo(g, CardW / 2f, 96 + (float)((1 - rise) * 70), 118, rise);
            double text = EaseOut((t - 0.25) / 0.5);
            Centered(g, "Solrino", fontTitle, Fade(Text1, text), 158);
            Centered(g, "Ready to install on this PC", fontBody, Fade(Text2, text), 195);

            double controls = EaseOut((t - 0.4) / 0.5);
            DrawCheckbox(g, controls);
            DrawButton(g, new RectangleF(CardW / 2f - 112, 262, 224, 46), "Install Solrino", controls, delegate { BeginInstall(); });
        }

        void DrawCheckbox(Graphics g, double alpha)
        {
            string label = "Create a desktop shortcut";
            var size = g.MeasureString(label, fontBody);
            float total = 16 + 8 + size.Width;
            float x = (CardW - total) / 2;
            float y = 226;
            var hit = new RectangleF(x - 6, y - 5, total + 12, 27);
            bool hover = hit.Contains(mouse);
            var box = new RectangleF(x, y, 16, 16);
            using (var path = Rounded(box, 4))
            {
                if (desktopShortcut)
                {
                    using (var fill = new SolidBrush(Fade(Red, alpha))) { g.FillPath(fill, path); }
                    using (var pen = new Pen(Fade(Color.White, alpha), 1.8f))
                    {
                        pen.StartCap = LineCap.Round;
                        pen.EndCap = LineCap.Round;
                        g.DrawLines(pen, new[] { new PointF(x + 3.6f, y + 8.2f), new PointF(x + 6.8f, y + 11.2f), new PointF(x + 12.4f, y + 5f) });
                    }
                }
                else
                {
                    using (var pen = new Pen(Fade(hover ? Text1 : Text2, alpha), 1.3f)) { g.DrawPath(pen, path); }
                }
            }
            using (var brush = new SolidBrush(Fade(hover ? Text1 : Text2, alpha)))
            {
                g.DrawString(label, fontBody, brush, x + 22, y - 2.5f);
            }
            hotspots.Add(new Hotspot { Rect = hit, Click = delegate { desktopShortcut = !desktopShortcut; } });
        }

        void DrawButton(Graphics g, RectangleF r, string label, double alpha, Action click)
        {
            bool hover = r.Contains(mouse);
            bool pressed = hover && mouseDown;
            if (pressed) { r.Inflate(-1.5f, -1); }

            if (hover)
            {
                for (int i = 10; i > 0; i -= 2)
                {
                    var glow = r;
                    glow.Inflate(i, i);
                    using (var path = Rounded(glow, 12 + i))
                    using (var brush = new SolidBrush(Fade(Color.FromArgb(14, 240, 30, 40), alpha)))
                    {
                        g.FillPath(brush, path);
                    }
                }
            }

            using (var path = Rounded(r, 11))
            {
                using (var fill = new LinearGradientBrush(r, Fade(hover ? RedBright : Red, alpha), Fade(RedDeep, alpha), 90f))
                {
                    g.FillPath(fill, path);
                }
                var state = g.Save();
                g.SetClip(path);
                double phase = (Now % 3.2) / 3.2;
                float sx = r.X - 80 + (float)(phase * (r.Width + 160));
                var band = new RectangleF(sx, r.Y, 80, r.Height);
                using (var shine = new LinearGradientBrush(band, Color.FromArgb(0, 255, 255, 255), Fade(Color.FromArgb(52, 255, 255, 255), alpha), 0f))
                {
                    var blend = new ColorBlend(3);
                    blend.Colors = new[] { Color.FromArgb(0, 255, 255, 255), Fade(Color.FromArgb(52, 255, 255, 255), alpha), Color.FromArgb(0, 255, 255, 255) };
                    blend.Positions = new[] { 0f, 0.5f, 1f };
                    shine.InterpolationColors = blend;
                    g.FillRectangle(shine, band);
                }
                g.Restore(state);
                using (var pen = new Pen(Fade(Color.FromArgb(90, 255, 150, 150), alpha), 1f)) { g.DrawPath(pen, path); }
            }
            using (var brush = new SolidBrush(Fade(Color.White, alpha)))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                format.LineAlignment = StringAlignment.Center;
                g.DrawString(label, fontButton, brush, r, format);
            }
            hotspots.Add(new Hotspot { Rect = r, Click = click });
        }

        void DrawInstalling(Graphics g)
        {
            double pulse = 1 + 0.025 * Math.Sin(Now * 3);
            DrawLogo(g, CardW / 2f, 78, (float)(84 * pulse), 1);
            Centered(g, "Installing Solrino", fontHeading, Text1, 130);
            Centered(g, (int)Math.Round(shownProgress * 100) + "%", fontPercent, Text1, 158);
            DrawProgress(g, new RectangleF(44, 236, CardW - 88, 12), shownProgress);

            string file = installer != null ? installer.CurrentFile : "";
            if (file.Length > 62) { file = "..." + file.Substring(file.Length - 59); }
            Centered(g, file, fontSmall, Text2, 260);
            Centered(g, "This only takes a moment", fontSmall, Fade(Text2, 0.7), 296);
        }

        void DrawProgress(Graphics g, RectangleF track, double value)
        {
            using (var path = Rounded(track, track.Height / 2))
            using (var brush = new SolidBrush(Color.FromArgb(150, 0, 0, 0)))
            {
                g.FillPath(brush, path);
            }

            float width = Math.Max(track.Height, (float)(track.Width * value));
            var bar = new RectangleF(track.X, track.Y, width, track.Height);
            for (int i = 8; i > 0; i -= 2)
            {
                var glow = bar;
                glow.Inflate(i, i);
                using (var path = Rounded(glow, track.Height / 2 + i))
                using (var brush = new SolidBrush(Color.FromArgb(13, 255, 40, 50)))
                {
                    g.FillPath(brush, path);
                }
            }
            using (var path = Rounded(bar, track.Height / 2))
            {
                var wide = new RectangleF(track.X, track.Y, track.Width + 1, track.Height);
                using (var fill = new LinearGradientBrush(wide, RedDeep, RedBright, 0f))
                {
                    g.FillPath(fill, path);
                }
                var state = g.Save();
                g.SetClip(path);
                double phase = (Now % 1.5) / 1.5;
                float sx = bar.X - 90 + (float)(phase * (bar.Width + 180));
                var band = new RectangleF(sx, bar.Y, 90, bar.Height);
                using (var shine = new LinearGradientBrush(band, Color.Transparent, Color.Transparent, 0f))
                {
                    var blend = new ColorBlend(3);
                    blend.Colors = new[] { Color.FromArgb(0, 255, 255, 255), Color.FromArgb(120, 255, 255, 255), Color.FromArgb(0, 255, 255, 255) };
                    blend.Positions = new[] { 0f, 0.5f, 1f };
                    shine.InterpolationColors = blend;
                    g.FillRectangle(shine, band);
                }
                g.Restore(state);
            }
        }

        void DrawDone(Graphics g)
        {
            double t = EaseOut((Now - screenStart) / 0.45);
            DrawLogo(g, CardW / 2f, 112, (float)(104 + 16 * t), 1);
            Centered(g, "You're all set", fontTitle, Fade(Text1, t), 186);
            Centered(g, options.Launch ? "Opening Solrino..." : "Solrino is installed", fontBody, Fade(Text2, t), 226);
            DrawProgress(g, new RectangleF(44, 276, CardW - 88, 8), 1);
        }

        void DrawProblem(Graphics g)
        {
            double t = EaseOut((Now - screenStart) / 0.35);
            DrawLogo(g, CardW / 2f, 72, 72, t);
            Centered(g, "That didn't finish", fontHeading, Fade(Text1, t), 118);
            using (var brush = new SolidBrush(Fade(Text2, t)))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                format.Trimming = StringTrimming.EllipsisWord;
                g.DrawString(problem, fontBody, brush, new RectangleF(44, 152, CardW - 88, 94), format);
            }
            DrawButton(g, new RectangleF(CardW / 2f - 92, 262, 184, 44), "Try again", t, delegate { BeginInstall(); });
        }

        void DrawClose(Graphics g)
        {
            var r = new RectangleF(CardW - 42, 12, 30, 30);
            bool hover = r.Contains(mouse);
            if (hover)
            {
                using (var path = Rounded(r, 8))
                using (var brush = new SolidBrush(Color.FromArgb(50, 255, 255, 255)))
                {
                    g.FillPath(brush, path);
                }
            }
            using (var pen = new Pen(hover ? Text1 : Text2, 1.6f))
            {
                pen.StartCap = LineCap.Round;
                pen.EndCap = LineCap.Round;
                g.DrawLine(pen, r.X + 10, r.Y + 10, r.Right - 10, r.Bottom - 10);
                g.DrawLine(pen, r.Right - 10, r.Y + 10, r.X + 10, r.Bottom - 10);
            }
            hotspots.Add(new Hotspot { Rect = r, Click = delegate { timer.Stop(); Close(); } });
        }

        // ------------------------------------------------------------ Mouse

        PointF ToCard(Point p)
        {
            return new PointF(p.X / scale - ShadowSize, p.Y / scale - ShadowSize);
        }

        Hotspot HotspotAt(PointF p)
        {
            for (int i = hotspots.Count - 1; i >= 0; i--)
            {
                if (hotspots[i].Rect.Contains(p)) { return hotspots[i]; }
            }
            return null;
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            mouse = ToCard(e.Location);
            if (dragging)
            {
                Location = new Point(Cursor.Position.X - dragOrigin.X, Cursor.Position.Y - dragOrigin.Y);
                return;
            }
            Cursor = HotspotAt(mouse) != null ? Cursors.Hand : Cursors.Default;
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            mouse = new PointF(-1, -1);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button != MouseButtons.Left) { return; }
            mouseDown = true;
            if (HotspotAt(ToCard(e.Location)) == null)
            {
                dragging = true;
                dragOrigin = new Point(Cursor.Position.X - Left, Cursor.Position.Y - Top);
            }
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (e.Button != MouseButtons.Left) { return; }
            mouseDown = false;
            if (dragging) { dragging = false; return; }
            var spot = HotspotAt(ToCard(e.Location));
            if (spot != null) { spot.Click(); }
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (screen == Screen.Import)
            {
                if (e.KeyCode == Keys.Enter) { AnswerImport(forks[chosenFork].Folder); }
                else if (e.KeyCode == Keys.Escape) { timer.Stop(); Close(); }
            }
            else if (screen == Screen.Welcome || screen == Screen.Problem)
            {
                if (e.KeyCode == Keys.Enter) { BeginInstall(); }
                else if (e.KeyCode == Keys.Escape) { timer.Stop(); Close(); }
            }
        }

        // ------------------------------------------------------------ Window

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            // Stopping halfway through would leave a broken install
            if (screen == Screen.Installing && e.CloseReason == CloseReason.UserClosing)
            {
                e.Cancel = true;
                return;
            }
            base.OnFormClosing(e);
        }

        void Present(Bitmap bitmap, double opacity)
        {
            IntPtr screenDc = Native.GetDC(IntPtr.Zero);
            IntPtr memDc = Native.CreateCompatibleDC(screenDc);
            IntPtr hBitmap = bitmap.GetHbitmap(Color.FromArgb(0));
            IntPtr old = Native.SelectObject(memDc, hBitmap);
            try
            {
                var size = new Native.SIZE { cx = bitmap.Width, cy = bitmap.Height };
                var source = new Native.POINT();
                var top = new Native.POINT { x = Left, y = Top };
                var blend = new Native.BLENDFUNCTION
                {
                    BlendOp = 0,
                    BlendFlags = 0,
                    SourceConstantAlpha = (byte)(255 * opacity),
                    AlphaFormat = 1
                };
                Native.UpdateLayeredWindow(Handle, screenDc, ref top, ref size, memDc, ref source, 0, ref blend, 2);
            }
            finally
            {
                Native.SelectObject(memDc, old);
                Native.DeleteObject(hBitmap);
                Native.DeleteDC(memDc);
                Native.ReleaseDC(IntPtr.Zero, screenDc);
            }
        }
    }

    static class Native
    {
        [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
        [StructLayout(LayoutKind.Sequential)] public struct SIZE { public int cx, cy; }
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct BLENDFUNCTION { public byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; }

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool UpdateLayeredWindow(IntPtr hwnd, IntPtr hdcDst, ref POINT pptDst, ref SIZE psize, IntPtr hdcSrc, ref POINT pptSrc, int crKey, ref BLENDFUNCTION pblend, int dwFlags);
        [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr hwnd);
        [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hwnd, IntPtr hdc);
        [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr hdc);
        [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr hdc);
        [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
        [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr obj);
    }

    static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new SetupWindow(Options.Parse(args)));
        }
    }
}
