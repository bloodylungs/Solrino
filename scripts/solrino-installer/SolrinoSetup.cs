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

            using (var stream = assembly.GetManifestResourceStream("uninstall.exe"))
            using (var output = File.Create(Path.Combine(root, UninstallerName)))
            {
                stream.CopyTo(output);
            }
            // Older versions uninstalled through a PowerShell script
            string script = Path.Combine(root, "uninstall.ps1");
            if (File.Exists(script)) { try { File.Delete(script); } catch (IOException) { } }

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

        public const string UninstallerName = "Uninstall Solrino.exe";

        /// Through Windows' own shell link object, not the script host
        static void MakeShortcut(string folder, string exe, string workingDirectory)
        {
            var link = (IShellLinkW)new ShellLink();
            link.SetPath(exe);
            link.SetWorkingDirectory(workingDirectory);
            link.SetIconLocation(exe, 0);
            link.SetDescription("Solrino chat client");
            ((IPersistFile)link).Save(Path.Combine(folder, "Solrino.lnk"), true);
        }

        [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
        class ShellLink { }

        [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
         Guid("000214F9-0000-0000-C000-000000000046")]
        interface IShellLinkW
        {
            void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder file, int max, IntPtr data, int flags);
            void GetIDList(out IntPtr list);
            void SetIDList(IntPtr list);
            void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder name, int max);
            void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
            void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder dir, int max);
            void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string dir);
            void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder args, int max);
            void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string args);
            void GetHotkey(out short hotkey);
            void SetHotkey(short hotkey);
            void GetShowCmd(out int show);
            void SetShowCmd(int show);
            void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder path, int max, out int index);
            void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string path, int index);
            void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, int reserved);
            void Resolve(IntPtr hwnd, int flags);
            void SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
        }

        [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
         Guid("0000010b-0000-0000-C000-000000000046")]
        interface IPersistFile
        {
            void GetClassID(out Guid id);
            [PreserveSig] int IsDirty();
            void Load([MarshalAs(UnmanagedType.LPWStr)] string file, int mode);
            void Save([MarshalAs(UnmanagedType.LPWStr)] string file, [MarshalAs(UnmanagedType.Bool)] bool remember);
            void SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string file);
            void GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string file);
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
                key.SetValue("UninstallString", "\"" + Path.Combine(root, UninstallerName) + "\"");
            }
        }
    }

    class Ember
    {
        public float X, Y, Speed, Size, Drift, Phase;
    }

    enum Backdrop { Minimal, Aurora, Gold, Bubbles, Classic, Halloween, Christmas, Summer, Nebula, Lava, Grid, Silk, Ripple, Paper, Shards, Chat, Neon, Spotlight, Hex, Mono }
    enum Bar { Glow, Thin, Segments, Chunky }
    /// How the pages are laid out: Classic is centered (or left-aligned with Left)
    enum Arrange { Classic, Split, Banner, Steps, Bar, Poster, Inset }

    /// Everything that gives the setup its look: colors, background, buttons
    /// and progress bar. Every page draws with it.
    class SetupTheme
    {
        public Color InkTop, InkBottom, Accent, AccentBright, AccentDeep, Text1, Text2, ButtonText, Border;
        public Backdrop Background;
        public Bar Progress;
        public bool Left;
        public bool TopLine = true;
        public string HeadFamily = "Segoe UI Semibold";
        public FontStyle HeadStyle = FontStyle.Regular;
        /// Corner roundness of buttons; -1 makes them fully round pills
        public float ButtonRadius = 11;
        public float ButtonAngle = 90f;
        public string Tagline = "Twitch chat, made yours.";
        /// A light card with dark text
        public bool Light;
        public Arrange Arrangement = Arrange.Classic;

        // The poster layout's giant logo: where it sits, how big, and extras behind it
        public float PosterX = 450, PosterY = 194, PosterSize = 330, PosterAlpha = 0.2f;
        /// Degrees per second the logo turns; 0 keeps it still
        public float PosterSpin;
        /// The logo on the left and the content on the right
        public bool PosterLeft;
        public bool PosterRays, PosterSlash;
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

        static readonly SetupTheme Theme = PickTheme();

        /// The look follows the seasons like the app's Seasonal theme: Halloween
        /// in October, Christmas in December, Summer from June to August, and
        /// the mirrored poster (look 27) the rest of the year.
        static SetupTheme PickTheme()
        {
            int look;
            if (!int.TryParse(Environment.GetEnvironmentVariable("SOLRINO_SETUP_LOOK"), out look))
            {
                int month = DateTime.Now.Month;
                look = month == 10 ? 6 : month == 12 ? 7 : (month >= 6 && month <= 8) ? 8 : 27;
            }
            switch (look)
            {
                case 5: // The original look: red embers rising into a soft glow
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 30, 13, 15), InkBottom = Color.FromArgb(255, 14, 8, 9),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 246, 238, 236), Text2 = Color.FromArgb(255, 172, 150, 148),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        Background = Backdrop.Classic, Progress = Bar.Glow, TopLine = false,
                    };
                case 6: // Halloween: a full moon, bats and a pumpkin glow
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 32, 16, 44), InkBottom = Color.FromArgb(255, 12, 8, 16),
                        Accent = Color.FromArgb(255, 255, 118, 22), AccentBright = Color.FromArgb(255, 255, 170, 80),
                        AccentDeep = Color.FromArgb(255, 170, 48, 20),
                        Text1 = Color.FromArgb(255, 250, 240, 232), Text2 = Color.FromArgb(255, 190, 166, 182),
                        ButtonText = Color.White, Border = Color.FromArgb(80, 255, 118, 22),
                        Background = Backdrop.Halloween, Progress = Bar.Glow, TopLine = false,
                        Tagline = "Spooky season is here.",
                    };
                case 7: // Christmas: evergreen, string lights and snow, candy-cane progress
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 14, 36, 27), InkBottom = Color.FromArgb(255, 7, 18, 13),
                        Accent = Color.FromArgb(255, 222, 38, 50), AccentBright = Color.FromArgb(255, 255, 112, 112),
                        AccentDeep = Color.FromArgb(255, 132, 16, 26),
                        Text1 = Color.FromArgb(255, 245, 250, 245), Text2 = Color.FromArgb(255, 172, 198, 180),
                        ButtonText = Color.White, Border = Color.FromArgb(80, 90, 170, 110),
                        Background = Backdrop.Christmas, Progress = Bar.Chunky, TopLine = false,
                        Tagline = "Happy holidays from Solrino.",
                    };
                case 8: // Summer: a sunset over the sea
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 18, 52, 74), InkBottom = Color.FromArgb(255, 66, 30, 56),
                        Accent = Color.FromArgb(255, 255, 132, 84), AccentBright = Color.FromArgb(255, 255, 204, 110),
                        AccentDeep = Color.FromArgb(255, 230, 74, 112),
                        Text1 = Color.FromArgb(255, 255, 248, 240), Text2 = Color.FromArgb(255, 214, 202, 204),
                        ButtonText = Color.White, Border = Color.FromArgb(80, 255, 180, 120),
                        Background = Backdrop.Summer, Progress = Bar.Glow, TopLine = false, ButtonAngle = 0f,
                        Tagline = "Summer vibes, all chat long.",
                    };
                case 9: // Crimson Nebula: slow red and magenta clouds with twinkling stars
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 20, 9, 18), InkBottom = Color.FromArgb(255, 9, 5, 10),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 248, 238, 242), Text2 = Color.FromArgb(255, 178, 150, 164),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 200, 40, 90),
                        Background = Backdrop.Nebula, Progress = Bar.Glow, TopLine = false, ButtonAngle = 0f,
                    };
                case 10: // Lava Lamp: warm blobs drifting up behind everything
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 26, 8, 8), InkBottom = Color.FromArgb(255, 12, 5, 5),
                        Accent = Color.FromArgb(255, 240, 40, 30), AccentBright = Color.FromArgb(255, 255, 130, 60),
                        AccentDeep = Color.FromArgb(255, 160, 12, 20),
                        Text1 = Color.FromArgb(255, 255, 242, 236), Text2 = Color.FromArgb(255, 196, 160, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 230, 70, 40),
                        Background = Backdrop.Lava, Progress = Bar.Chunky, TopLine = false,
                        ButtonRadius = -1, ButtonAngle = 0f,
                    };
                case 11: // Red Grid: a retro grid floor rolling toward you under a glowing horizon
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 18, 6, 12), InkBottom = Color.FromArgb(255, 6, 3, 6),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 255, 236, 238), Text2 = Color.FromArgb(255, 186, 140, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(90, 255, 40, 70),
                        Background = Backdrop.Grid, Progress = Bar.Segments, TopLine = false,
                        HeadFamily = "Bahnschrift", ButtonRadius = 3, ButtonAngle = 0f,
                    };
                case 12: // Red Silk: flowing ribbons of thin lines, with elegant italic headings
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 24, 10, 13), InkBottom = Color.FromArgb(255, 11, 6, 8),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 248, 238, 236), Text2 = Color.FromArgb(255, 176, 152, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(60, 200, 50, 60),
                        Background = Backdrop.Silk, Progress = Bar.Thin, TopLine = false,
                        HeadFamily = "Palatino Linotype", HeadStyle = FontStyle.Italic,
                    };
                case 13: // Pulse: soft rings ripple out from the top, like a heartbeat
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 22, 10, 12), InkBottom = Color.FromArgb(255, 10, 6, 7),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 246, 238, 236), Text2 = Color.FromArgb(255, 172, 150, 148),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        Background = Backdrop.Ripple, Progress = Bar.Glow, TopLine = false,
                        ButtonRadius = -1,
                    };
                case 14: // Paper: a light card, clean and bright, with Solrino red
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 253, 251, 251), InkBottom = Color.FromArgb(255, 242, 236, 236),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 32, 22, 24), Text2 = Color.FromArgb(255, 112, 96, 98),
                        ButtonText = Color.White, Border = Color.FromArgb(255, 226, 212, 214),
                        Background = Backdrop.Paper, Progress = Bar.Thin, TopLine = true,
                        ButtonRadius = 8, Light = true,
                    };
                case 15: // Shards: a field of red glass triangles catching the light
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 8, 22, 28), InkBottom = Color.FromArgb(255, 4, 10, 14),
                        Accent = Color.FromArgb(255, 30, 190, 210), AccentBright = Color.FromArgb(255, 120, 235, 245),
                        AccentDeep = Color.FromArgb(255, 20, 100, 150),
                        Text1 = Color.FromArgb(255, 236, 250, 252), Text2 = Color.FromArgb(255, 150, 186, 194),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 60, 200, 220),
                        Background = Backdrop.Shards, Progress = Bar.Glow, TopLine = false, ButtonRadius = 6, ButtonAngle = 0f,
                    };
                case 16: // Chat: little chat bubbles floating up the sides
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 26, 14, 44), InkBottom = Color.FromArgb(255, 12, 8, 22),
                        Accent = Color.FromArgb(255, 145, 70, 255), AccentBright = Color.FromArgb(255, 195, 150, 255),
                        AccentDeep = Color.FromArgb(255, 90, 40, 190),
                        Text1 = Color.FromArgb(255, 244, 238, 255), Text2 = Color.FromArgb(255, 176, 160, 206),
                        ButtonText = Color.White, Border = Color.FromArgb(80, 145, 90, 255),
                        Background = Backdrop.Chat, Progress = Bar.Glow, TopLine = false, ButtonRadius = 14,
                    };
                case 17: // Neon: a glowing red neon tube around a dark brick wall
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 12, 10, 26), InkBottom = Color.FromArgb(255, 5, 5, 12),
                        Accent = Color.FromArgb(255, 255, 40, 160), AccentBright = Color.FromArgb(255, 255, 150, 215),
                        AccentDeep = Color.FromArgb(255, 120, 40, 220),
                        Text1 = Color.FromArgb(255, 255, 240, 250), Text2 = Color.FromArgb(255, 190, 160, 200),
                        ButtonText = Color.White, Border = Color.FromArgb(60, 255, 60, 180),
                        Background = Backdrop.Neon, Progress = Bar.Glow, TopLine = false, ButtonRadius = -1,
                    };
                case 18: // Spotlight: stage lights sweeping over velvet curtains
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 28, 8, 12), InkBottom = Color.FromArgb(255, 10, 4, 6),
                        Accent = Color.FromArgb(255, 230, 170, 60), AccentBright = Color.FromArgb(255, 255, 215, 120),
                        AccentDeep = Color.FromArgb(255, 170, 100, 30),
                        Text1 = Color.FromArgb(255, 255, 244, 232), Text2 = Color.FromArgb(255, 196, 168, 156),
                        ButtonText = Color.FromArgb(255, 40, 20, 8), Border = Color.FromArgb(70, 230, 170, 60),
                        Background = Backdrop.Spotlight, Progress = Bar.Glow, TopLine = false, HeadFamily = "Georgia", HeadStyle = FontStyle.Bold,
                    };
                case 19: // Hex: a honeycomb with waves of light moving through it
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 8, 12, 26), InkBottom = Color.FromArgb(255, 4, 6, 12),
                        Accent = Color.FromArgb(255, 40, 140, 255), AccentBright = Color.FromArgb(255, 120, 200, 255),
                        AccentDeep = Color.FromArgb(255, 40, 60, 200),
                        Text1 = Color.FromArgb(255, 236, 244, 255), Text2 = Color.FromArgb(255, 150, 168, 196),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 60, 140, 255),
                        Background = Backdrop.Hex, Progress = Bar.Segments, TopLine = false, ButtonRadius = 4, ButtonAngle = 0f,
                    };
                case 20: // Mono: bold black and white with a red stripe and a giant name scrolling behind
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 12, 12, 12), InkBottom = Color.FromArgb(255, 6, 6, 6),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 250, 250, 250), Text2 = Color.FromArgb(255, 150, 150, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(60, 255, 255, 255),
                        Background = Backdrop.Mono, Progress = Bar.Thin, TopLine = false, HeadFamily = "Segoe UI Black", ButtonRadius = 0, ButtonAngle = 0f, Left = true,
                    };
                case 21: // Split: a red panel on the left with the logo, content on the right
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 24, 14, 16), InkBottom = Color.FromArgb(255, 12, 8, 9),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 246, 238, 236), Text2 = Color.FromArgb(255, 172, 150, 148),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        Background = Backdrop.Minimal, Progress = Bar.Glow, TopLine = false, ButtonRadius = 10,
                        Arrangement = Arrange.Split,
                    };
                case 22: // Banner: a teal strip across the top on dark slate
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 18, 25, 33), InkBottom = Color.FromArgb(255, 10, 14, 20),
                        Accent = Color.FromArgb(255, 20, 184, 166), AccentBright = Color.FromArgb(255, 94, 234, 212),
                        AccentDeep = Color.FromArgb(255, 14, 100, 140),
                        Text1 = Color.FromArgb(255, 236, 244, 248), Text2 = Color.FromArgb(255, 150, 170, 182),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 60, 180, 180),
                        Background = Backdrop.Minimal, Progress = Bar.Thin, TopLine = false, ButtonRadius = 8, ButtonAngle = 0f,
                        Arrangement = Arrange.Banner,
                    };
                case 23: // Steps: a light card with a step list down the left
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 253, 251, 251), InkBottom = Color.FromArgb(255, 244, 239, 239),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 32, 22, 24), Text2 = Color.FromArgb(255, 112, 96, 98),
                        ButtonText = Color.White, Border = Color.FromArgb(255, 226, 212, 214),
                        Background = Backdrop.Minimal, Progress = Bar.Thin, TopLine = false, ButtonRadius = 8,
                        Light = true, Arrangement = Arrange.Steps,
                    };
                case 24: // Bar: graphite and amber, with the buttons in a bar along the bottom
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 30, 30, 33), InkBottom = Color.FromArgb(255, 19, 19, 21),
                        Accent = Color.FromArgb(255, 245, 158, 11), AccentBright = Color.FromArgb(255, 252, 211, 77),
                        AccentDeep = Color.FromArgb(255, 217, 119, 6),
                        Text1 = Color.FromArgb(255, 245, 245, 244), Text2 = Color.FromArgb(255, 160, 158, 154),
                        ButtonText = Color.FromArgb(255, 34, 22, 4), Border = Color.FromArgb(60, 255, 255, 255),
                        Background = Backdrop.Minimal, Progress = Bar.Segments, TopLine = false, ButtonRadius = 6, ButtonAngle = 0f,
                        Arrangement = Arrange.Bar,
                    };
                case 25: // Poster: the logo huge on the right, bold text on the left
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 16, 7, 9), InkBottom = Color.FromArgb(255, 6, 3, 4),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 250, 244, 242), Text2 = Color.FromArgb(255, 176, 152, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        Background = Backdrop.Classic, Progress = Bar.Glow, TopLine = false,
                        HeadFamily = "Segoe UI Black", ButtonRadius = 4, ButtonAngle = 0f,
                        Arrangement = Arrange.Poster,
                    };
                case 26: // Inset: a floating inner card over drifting aurora lights
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 14, 22, 42), InkBottom = Color.FromArgb(255, 7, 10, 22),
                        Accent = Color.FromArgb(255, 120, 90, 255), AccentBright = Color.FromArgb(255, 170, 150, 255),
                        AccentDeep = Color.FromArgb(255, 40, 150, 200),
                        Text1 = Color.FromArgb(255, 236, 240, 255), Text2 = Color.FromArgb(255, 160, 166, 200),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 120, 120, 220),
                        Background = Backdrop.Aurora, Progress = Bar.Glow, TopLine = false, ButtonRadius = -1, ButtonAngle = 0f,
                        Arrangement = Arrange.Inset,
                    };
                case 27: // Poster, mirrored: the giant logo on the left, content on the right
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 16, 7, 9), InkBottom = Color.FromArgb(255, 6, 3, 4),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 250, 244, 242), Text2 = Color.FromArgb(255, 176, 152, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        HeadFamily = "Segoe UI Black", ButtonAngle = 0f, Arrangement = Arrange.Poster,
                        Background = Backdrop.Classic, Progress = Bar.Glow, TopLine = false, ButtonRadius = 4,
                        PosterLeft = true, PosterX = 46, PosterY = 176, PosterSize = 280, PosterAlpha = 0.24f,
                    };
                case 28: // Poster, spinning: a huge logo turning slowly in the corner with beams of light
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 14, 6, 8), InkBottom = Color.FromArgb(255, 5, 2, 3),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 250, 244, 242), Text2 = Color.FromArgb(255, 176, 152, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        HeadFamily = "Segoe UI Black", ButtonAngle = 0f, Arrangement = Arrange.Poster,
                        Background = Backdrop.Classic, Progress = Bar.Glow, TopLine = false, ButtonRadius = 4,
                        PosterX = 490, PosterY = 320, PosterSize = 440, PosterAlpha = 0.24f, PosterSpin = 6, PosterRays = true,
                    };
                case 29: // Poster, slash: a bold diagonal red band behind the logo
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 12, 10, 11), InkBottom = Color.FromArgb(255, 5, 5, 6),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 250, 250, 250), Text2 = Color.FromArgb(255, 160, 156, 158),
                        ButtonText = Color.White, Border = Color.FromArgb(50, 255, 255, 255),
                        HeadFamily = "Segoe UI Black", ButtonAngle = 0f, Arrangement = Arrange.Poster,
                        Background = Backdrop.Minimal, Progress = Bar.Thin, TopLine = false, ButtonRadius = 0,
                        PosterX = 450, PosterY = 190, PosterSize = 260, PosterAlpha = 0.55f, PosterSlash = true,
                    };
                case 30: // Poster, cobalt: deep blue with the red logo glowing through
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 10, 16, 36), InkBottom = Color.FromArgb(255, 4, 7, 18),
                        Accent = Color.FromArgb(255, 40, 110, 255), AccentBright = Color.FromArgb(255, 110, 170, 255),
                        AccentDeep = Color.FromArgb(255, 30, 50, 170),
                        Text1 = Color.FromArgb(255, 238, 244, 255), Text2 = Color.FromArgb(255, 150, 166, 200),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 70, 120, 255),
                        HeadFamily = "Segoe UI Black", ButtonAngle = 0f, Arrangement = Arrange.Poster,
                        Background = Backdrop.Minimal, Progress = Bar.Glow, TopLine = false, ButtonRadius = 4,
                        PosterX = 450, PosterY = 194, PosterSize = 340, PosterAlpha = 0.3f,
                    };
                case 31: // Poster, gold: black and gold with beams behind the logo
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 20, 17, 13), InkBottom = Color.FromArgb(255, 8, 7, 6),
                        Accent = Color.FromArgb(255, 205, 160, 80), AccentBright = Color.FromArgb(255, 240, 205, 130),
                        AccentDeep = Color.FromArgb(255, 150, 105, 40),
                        Text1 = Color.FromArgb(255, 246, 238, 222), Text2 = Color.FromArgb(255, 170, 155, 128),
                        ButtonText = Color.FromArgb(255, 30, 22, 10), Border = Color.FromArgb(70, 205, 160, 80),
                        HeadFamily = "Segoe UI Black", ButtonAngle = 0f, Arrangement = Arrange.Poster,
                        Background = Backdrop.Gold, Progress = Bar.Segments, TopLine = false, ButtonRadius = 2,
                        PosterX = 450, PosterY = 194, PosterSize = 320, PosterAlpha = 0.22f, PosterRays = true,
                    };
                case 32: // Poster, rising: the logo peeking up from the bottom, wrapped in embers
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 22, 8, 10), InkBottom = Color.FromArgb(255, 8, 3, 4),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 250, 244, 242), Text2 = Color.FromArgb(255, 176, 152, 150),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        HeadFamily = "Segoe UI Black", ButtonAngle = 0f, Arrangement = Arrange.Poster,
                        Background = Backdrop.Classic, Progress = Bar.Chunky, TopLine = false, ButtonRadius = -1,
                        PosterX = 440, PosterY = 330, PosterSize = 380, PosterAlpha = 0.3f, PosterRays = true,
                    };
                case 2: // Aurora: deep navy with teal and violet lights
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 14, 22, 42), InkBottom = Color.FromArgb(255, 7, 10, 22),
                        Accent = Color.FromArgb(255, 40, 190, 175), AccentBright = Color.FromArgb(255, 110, 240, 215),
                        AccentDeep = Color.FromArgb(255, 70, 70, 190),
                        Text1 = Color.FromArgb(255, 236, 244, 255), Text2 = Color.FromArgb(255, 150, 168, 196),
                        ButtonText = Color.White, Border = Color.FromArgb(80, 80, 180, 200),
                        Background = Backdrop.Aurora, Progress = Bar.Glow, ButtonAngle = 0f,
                    };
                case 3: // Midnight Gold: black and gold, with serif headings
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 22, 19, 15), InkBottom = Color.FromArgb(255, 9, 8, 7),
                        Accent = Color.FromArgb(255, 205, 160, 80), AccentBright = Color.FromArgb(255, 240, 205, 130),
                        AccentDeep = Color.FromArgb(255, 150, 105, 40),
                        Text1 = Color.FromArgb(255, 246, 238, 222), Text2 = Color.FromArgb(255, 170, 155, 128),
                        ButtonText = Color.FromArgb(255, 30, 22, 10), Border = Color.FromArgb(70, 205, 160, 80),
                        Background = Backdrop.Gold, Progress = Bar.Segments, TopLine = false,
                        HeadFamily = "Georgia", HeadStyle = FontStyle.Bold, ButtonRadius = 4,
                    };
                case 4: // Bubblegum: plum with pink and purple bubbles
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 44, 18, 54), InkBottom = Color.FromArgb(255, 22, 10, 32),
                        Accent = Color.FromArgb(255, 255, 90, 170), AccentBright = Color.FromArgb(255, 255, 150, 210),
                        AccentDeep = Color.FromArgb(255, 150, 70, 235),
                        Text1 = Color.FromArgb(255, 255, 240, 250), Text2 = Color.FromArgb(255, 205, 172, 206),
                        ButtonText = Color.White, Border = Color.FromArgb(80, 255, 120, 200),
                        Background = Backdrop.Bubbles, Progress = Bar.Chunky, TopLine = false,
                        ButtonRadius = -1, ButtonAngle = 0f,
                    };
                default: // Ember Minimal: dark and calm, left-aligned, in Solrino red
                    return new SetupTheme
                    {
                        InkTop = Color.FromArgb(255, 26, 15, 17), InkBottom = Color.FromArgb(255, 15, 9, 10),
                        Accent = Color.FromArgb(255, 227, 16, 28), AccentBright = Color.FromArgb(255, 255, 72, 72),
                        AccentDeep = Color.FromArgb(255, 126, 8, 18),
                        Text1 = Color.FromArgb(255, 246, 238, 236), Text2 = Color.FromArgb(255, 172, 150, 148),
                        ButtonText = Color.White, Border = Color.FromArgb(70, 170, 30, 40),
                        Background = Backdrop.Minimal, Progress = Bar.Thin, Left = true,
                    };
            }
        }

        bool preview;
        string previewFile = "";

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
        Font fontTitle, fontHeading, fontBody, fontSmall, fontButton, fontPercent, fontHero, fontEyebrow;

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
            fontTitle = new Font(Theme.HeadFamily, 26f, Theme.HeadStyle, GraphicsUnit.Pixel);
            fontHeading = new Font(Theme.HeadFamily, 19f, Theme.HeadStyle, GraphicsUnit.Pixel);
            fontBody = new Font(family, 13.5f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontSmall = new Font(family, 11.5f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontButton = new Font(family + " Semibold", 15f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontPercent = new Font(family + " Light", 44f, FontStyle.Regular, GraphicsUnit.Pixel);
            fontHero = new Font(Theme.HeadFamily, 34f, Theme.HeadStyle, GraphicsUnit.Pixel);
            fontEyebrow = new Font(family + " Semibold", 11f, FontStyle.Regular, GraphicsUnit.Pixel);

            for (int i = 0; i < 34; i++) { embers.Add(NewEmber(true)); }

            // Only the first time, and only if there's something to bring over
            if (!options.Auto && ForkProfile.IsFirstInstall(options))
            {
                forks = ForkProfile.Find(options);
                if (forks.Count > 3) { forks.RemoveRange(3, forks.Count - 3); }
                if (forks.Count > 0) { screen = Screen.Import; }
            }

            string page = Environment.GetEnvironmentVariable("SOLRINO_SETUP_PAGE");
            if (!string.IsNullOrEmpty(page))
            {
                preview = true;
                switch (page)
                {
                    case "import":
                        forks = new List<ForkProfile>
                        {
                            new ForkProfile { Name = "Moltorino / Chatterino", LastUsed = "Last used Nov 9, 2026", Folder = "a" },
                            new ForkProfile { Name = "Bluzyrino", LastUsed = "Last used Oct 2, 2026", Folder = "b" },
                        };
                        chosenFork = 0;
                        screen = Screen.Import;
                        break;
                    case "installing":
                        screen = Screen.Installing;
                        shownProgress = 0.62;
                        previewFile = "Copying Qt6Gui.dll";
                        break;
                    case "done": screen = Screen.Done; break;
                    case "problem":
                        screen = Screen.Problem;
                        problem = Environment.GetEnvironmentVariable("SOLRINO_SETUP_PROBLEM") ??
                                  "Solrino is still open. Right-click its icon near the clock, choose Quit, then try again.";
                        break;
                    default: screen = Screen.Welcome; break;
                }
            }

            timer.Interval = 16;
            timer.Tick += delegate { Tick(); };
            Shown += delegate
            {
                Native.SetWindowPos(Handle, IntPtr.Zero, Left, Top, 0, 0, 0x0001 | 0x0004 | 0x0010 /* NOSIZE NOZORDER NOACTIVATE */);
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

            if (!preview && screen == Screen.Done && fadeOutStart < 0 && now - doneAt > 1.5)
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

            if (!moving) { Render(); }
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
            radius = Math.Max(0.5f, Math.Min(radius, Math.Min(r.Width, r.Height) / 2));
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

                // Preview only: save one frame to a file and close, for comparing looks
                string snapshot = preview ? Environment.GetEnvironmentVariable("SOLRINO_SETUP_SNAPSHOT") : null;
                if (!string.IsNullOrEmpty(snapshot) && Now > 2.2)
                {
                    surface.Save(snapshot, System.Drawing.Imaging.ImageFormat.Png);
                    timer.Stop();
                    BeginInvoke(new Action(Close));
                    return;
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
                using (var fill = new LinearGradientBrush(card, Theme.InkTop, Theme.InkBottom, 90f))
                {
                    g.FillPath(fill, path);
                }
                var state = g.Save();
                g.SetClip(path);
                DrawBackground(g, card);
                g.Restore(state);
                using (var pen = new Pen(Theme.Border, 1f))
                {
                    g.DrawPath(pen, path);
                }
            }

            if (Theme.Arrangement != Arrange.Classic) { DrawArranged(g); }
            else switch (screen)
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

        // ------------------------------------------------------------ Backgrounds

        void DrawBackground(Graphics g, RectangleF card)
        {
            switch (Theme.Background)
            {
                case Backdrop.Aurora: DrawAurora(g); break;
                case Backdrop.Gold: DrawGoldFrame(g); break;
                case Backdrop.Bubbles: DrawBubbles(g); break;
                case Backdrop.Classic: DrawClassic(g); break;
                case Backdrop.Halloween: DrawHalloween(g); break;
                case Backdrop.Christmas: DrawChristmas(g); break;
                case Backdrop.Summer: DrawSummer(g); break;
                case Backdrop.Nebula: DrawNebula(g); break;
                case Backdrop.Lava: DrawLava(g); break;
                case Backdrop.Grid: DrawGrid(g); break;
                case Backdrop.Silk: DrawSilk(g); break;
                case Backdrop.Ripple: DrawRipple(g); break;
                case Backdrop.Paper: DrawPaper(g); break;
                case Backdrop.Shards: DrawShards(g); break;
                case Backdrop.Chat: DrawChat(g); break;
                case Backdrop.Neon: DrawNeon(g); break;
                case Backdrop.Spotlight: DrawSpotlight(g); break;
                case Backdrop.Hex: DrawHex(g); break;
                case Backdrop.Mono: DrawMono(g); break;
                default: DrawCornerLight(g, CardW - 40, -20, 260, 46); break;
            }
            if (Theme.TopLine) { DrawTopLine(g); }
        }

        void DrawCornerLight(Graphics g, float cx, float cy, float radius, int alpha)
        {
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(cx - radius, cy - radius, radius * 2, radius * 2);
                using (var brush = new PathGradientBrush(path))
                {
                    brush.CenterColor = Color.FromArgb(alpha, Theme.Accent);
                    brush.SurroundColors = new[] { Color.FromArgb(0, Theme.Accent) };
                    g.FillPath(brush, path);
                }
            }
        }

        /// A thin bright line along the top edge, brightest on the left
        void DrawTopLine(Graphics g)
        {
            using (var brush = new LinearGradientBrush(new RectangleF(0, 0, CardW + 1, 2), Color.Transparent, Color.Transparent, 0f))
            {
                var blend = new ColorBlend(4);
                blend.Colors = new[] { Color.FromArgb(0, Theme.AccentBright), Color.FromArgb(220, Theme.AccentBright), Color.FromArgb(60, Theme.AccentBright), Color.FromArgb(0, Theme.AccentBright) };
                blend.Positions = new[] { 0f, 0.28f, 0.7f, 1f };
                brush.InterpolationColors = blend;
                g.FillRectangle(brush, new RectangleF(0, 0, CardW, 2));
            }
        }

        /// Aurora: soft ribbons of light drifting across the top, and a few stars
        void DrawAurora(Graphics g)
        {
            var colors = new[] { Color.FromArgb(64, 60, 230, 200), Color.FromArgb(46, 150, 100, 255) };
            for (int k = 0; k < 2; k++)
            {
                var points = new List<PointF>();
                float baseY = 34 + k * 30;
                for (int i = 0; i <= 40; i++)
                {
                    float x = CardW * i / 40f;
                    points.Add(new PointF(x, baseY + (float)(14 * Math.Sin(i * 0.32 + Now * (0.5 + k * 0.2) + k * 2))));
                }
                for (int i = 40; i >= 0; i--)
                {
                    float x = CardW * i / 40f;
                    points.Add(new PointF(x, baseY + 46 + (float)(10 * Math.Sin(i * 0.28 + Now * 0.4 + k))));
                }
                var bounds = new RectangleF(0, baseY - 16, CardW, 80);
                using (var brush = new LinearGradientBrush(bounds, Color.Transparent, Color.Transparent, 90f))
                {
                    var blend = new ColorBlend(3);
                    blend.Colors = new[] { Color.FromArgb(0, colors[k]), colors[k], Color.FromArgb(0, colors[k]) };
                    blend.Positions = new[] { 0f, 0.4f, 1f };
                    brush.InterpolationColors = blend;
                    g.FillPolygon(brush, points.ToArray());
                }
            }
            foreach (var e in embers)
            {
                if (e.Y > CardH * 0.55f) { continue; }
                int alpha = (int)(50 + 80 * (0.5 + 0.5 * Math.Sin(Now * 1.2 + e.Phase)));
                using (var brush = new SolidBrush(Color.FromArgb(alpha, 230, 240, 255)))
                {
                    g.FillEllipse(brush, e.X, e.Y * 0.5f + 6, e.Size * 0.7f, e.Size * 0.7f);
                }
            }
        }

        /// Midnight Gold: a thin double frame with small diamonds, and sparkles
        void DrawGoldFrame(Graphics g)
        {
            using (var pen = new Pen(Color.FromArgb(90, Theme.Accent), 1f))
            using (var path = Rounded(new RectangleF(10, 10, CardW - 20, CardH - 20), 10))
            {
                g.DrawPath(pen, path);
            }
            using (var pen = new Pen(Color.FromArgb(36, Theme.Accent), 1f))
            using (var path = Rounded(new RectangleF(15, 15, CardW - 30, CardH - 30), 7))
            {
                g.DrawPath(pen, path);
            }
            foreach (var at in new[] { new PointF(CardW / 2f, 10), new PointF(CardW / 2f, CardH - 10) })
            {
                var diamond = new[] { new PointF(at.X, at.Y - 4), new PointF(at.X + 4, at.Y), new PointF(at.X, at.Y + 4), new PointF(at.X - 4, at.Y) };
                using (var brush = new SolidBrush(Theme.Accent)) { g.FillPolygon(brush, diamond); }
            }
            foreach (var e in embers)
            {
                double twinkle = Math.Pow(Math.Max(0, Math.Sin(Now * 0.9 + e.Phase)), 6);
                if (twinkle < 0.05) { continue; }
                float sx = e.X, sy = (e.Phase / 6.28f) * CardH;
                if (sx > 80 && sx < CardW - 80 && sy > 40 && sy < CardH - 40) { continue; }
                using (var brush = new SolidBrush(Color.FromArgb((int)(170 * twinkle), Theme.AccentBright)))
                {
                    float x = sx, y = sy, r = 1.2f + e.Size * 0.5f;
                    g.FillEllipse(brush, x - r, y - r, r * 2, r * 2);
                }
            }
        }

        void DrawSoftLight(Graphics g, float cx, float cy, float rx, float ry, Color color)
        {
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
                using (var brush = new PathGradientBrush(path))
                {
                    brush.CenterColor = color;
                    brush.SurroundColors = new[] { Color.FromArgb(0, color) };
                    g.FillPath(brush, path);
                }
            }
        }

        void DrawRisingEmbers(Graphics g, Color color)
        {
            foreach (var e in embers)
            {
                float x = e.X + (float)Math.Sin(Now * 0.8 + e.Phase) * e.Drift;
                double life = Math.Max(0, Math.Min(1, e.Y / CardH));
                int alpha = (int)(40 + 150 * life * (0.6 + 0.4 * Math.Sin(Now * 2.2 + e.Phase)));
                using (var brush = new SolidBrush(Color.FromArgb(Math.Max(0, Math.Min(255, alpha)), color)))
                {
                    g.FillEllipse(brush, x, e.Y, e.Size, e.Size);
                }
            }
        }

        /// The original look: a breathing red glow with embers rising through it
        void DrawClassic(Graphics g)
        {
            double pulse = 0.5 + 0.5 * Math.Sin(Now * 1.3);
            DrawSoftLight(g, CardW / 2f, 92, 280, 170, Color.FromArgb((int)(62 + 26 * pulse), 200, 18, 30));
            DrawRisingEmbers(g, Color.FromArgb(255, 255, 70, 50));
        }

        /// A little bat, wings out, facing the way it flies
        void DrawBat(Graphics g, float cx, float cy, float width, double flap, Color color)
        {
            float w = width / 2, lift = (float)(flap * width * 0.18);
            var wing = new[]
            {
                new PointF(cx, cy - width * 0.06f),
                new PointF(cx - w * 0.45f, cy - width * 0.2f - lift),
                new PointF(cx - w, cy - width * 0.08f - lift),
                new PointF(cx - w * 0.78f, cy + width * 0.04f),
                new PointF(cx - w * 0.55f, cy - width * 0.01f),
                new PointF(cx - w * 0.32f, cy + width * 0.08f),
                new PointF(cx, cy + width * 0.06f),
                new PointF(cx + w * 0.32f, cy + width * 0.08f),
                new PointF(cx + w * 0.55f, cy - width * 0.01f),
                new PointF(cx + w * 0.78f, cy + width * 0.04f),
                new PointF(cx + w, cy - width * 0.08f - lift),
                new PointF(cx + w * 0.45f, cy - width * 0.2f - lift),
            };
            using (var brush = new SolidBrush(color))
            {
                g.FillPolygon(brush, wing);
                g.FillEllipse(brush, cx - width * 0.07f, cy - width * 0.12f, width * 0.14f, width * 0.22f);
            }
        }

        /// Halloween: a full moon, bats flying past it, and a pumpkin glow
        void DrawHalloween(Graphics g)
        {
            DrawSoftLight(g, CardW / 2f, CardH + 30, 330, 150, Color.FromArgb(60, 255, 110, 20));
            var moon = new PointF(CardW - 74, 62);
            DrawSoftLight(g, moon.X, moon.Y, 90, 90, Color.FromArgb(46, 255, 214, 140));
            using (var brush = new SolidBrush(Color.FromArgb(200, 250, 226, 170)))
            {
                g.FillEllipse(brush, moon.X - 26, moon.Y - 26, 52, 52);
            }
            using (var brush = new SolidBrush(Color.FromArgb(60, 200, 160, 110)))
            {
                g.FillEllipse(brush, moon.X - 12, moon.Y - 10, 10, 9);
                g.FillEllipse(brush, moon.X + 6, moon.Y + 6, 7, 6);
            }
            // Bats drift slowly across, flapping gently
            for (int i = 0; i < 3; i++)
            {
                float travel = (float)((Now * (16 + i * 5) + i * 190) % (CardW + 80)) - 40;
                float y = 40 + i * 22 + (float)(6 * Math.Sin(Now * 1.1 + i));
                DrawBat(g, travel, y, 26 - i * 5, Math.Sin(Now * 5 + i), Color.FromArgb(230, 14, 6, 18));
            }
            DrawRisingEmbers(g, Color.FromArgb(255, 255, 140, 40));
        }

        /// Christmas: lights strung along the top, and snow falling
        void DrawChristmas(Graphics g)
        {
            DrawSoftLight(g, CardW / 2f, -20, 320, 140, Color.FromArgb(46, 220, 40, 50));
            foreach (var e in embers)
            {
                // The same particles, falling instead of rising
                float x = e.X + (float)Math.Sin(Now * 0.6 + e.Phase) * e.Drift;
                float y = CardH - e.Y;
                float size = 1.2f + e.Size * 0.9f;
                using (var brush = new SolidBrush(Color.FromArgb(150, 240, 248, 255)))
                {
                    g.FillEllipse(brush, x, y, size, size);
                }
            }
            // A drift of snow along the bottom
            using (var path = new GraphicsPath())
            {
                path.AddBezier(0, CardH - 18, CardW * 0.3f, CardH - 34, CardW * 0.6f, CardH - 6, CardW, CardH - 24);
                path.AddLine(CardW, CardH - 24, CardW, CardH);
                path.AddLine(CardW, CardH, 0, CardH);
                path.CloseFigure();
                using (var brush = new SolidBrush(Color.FromArgb(60, 230, 240, 255))) { g.FillPath(brush, path); }
            }
            // String lights: a drooping wire with bulbs that glow softly
            var bulbs = new[] { Color.FromArgb(255, 255, 70, 80), Color.FromArgb(255, 80, 220, 120), Color.FromArgb(255, 255, 200, 70), Color.FromArgb(255, 90, 170, 255) };
            float span = CardW / 4f;
            using (var pen = new Pen(Color.FromArgb(200, 30, 50, 36), 1.4f))
            {
                for (int k = 0; k < 4; k++)
                {
                    g.DrawBezier(pen, k * span, 6, k * span + span * 0.3f, 22, k * span + span * 0.7f, 22, (k + 1) * span, 6);
                }
            }
            int index = 0;
            for (float x = span * 0.12f; x < CardW; x += span / 4f, index++)
            {
                float along = (x % span) / span;
                float wireY = 6 + 16 * (float)Math.Sin(along * Math.PI) * 0.94f;
                var color = bulbs[index % 4];
                double shine = 0.75 + 0.25 * Math.Sin(Now * 1.4 + index);
                DrawSoftLight(g, x, wireY + 7, 12, 12, Color.FromArgb((int)(80 * shine), color));
                using (var brush = new SolidBrush(Color.FromArgb((int)(255 * shine), color)))
                {
                    g.FillEllipse(brush, x - 3, wireY + 2, 6, 9);
                }
            }
        }

        /// Summer: a low sun over gently moving waves
        void DrawSummer(Graphics g)
        {
            var sun = new PointF(CardW * 0.8f, CardH - 64);
            DrawSoftLight(g, sun.X, sun.Y, 150, 120, Color.FromArgb(90, 255, 170, 80));
            using (var path = new GraphicsPath())
            {
                path.AddEllipse(sun.X - 44, sun.Y - 44, 88, 88);
                using (var brush = new PathGradientBrush(path))
                {
                    brush.CenterColor = Color.FromArgb(230, 255, 230, 140);
                    brush.SurroundColors = new[] { Color.FromArgb(200, 255, 140, 80) };
                    g.FillPath(brush, path);
                }
            }
            // Waves along the bottom, over the lower half of the sun
            var waveColors = new[] { Color.FromArgb(150, 30, 90, 120), Color.FromArgb(190, 20, 60, 90) };
            for (int k = 0; k < 2; k++)
            {
                var points = new List<PointF>();
                float baseY = CardH - 56 + k * 22;
                for (int i = 0; i <= 40; i++)
                {
                    float x = CardW * i / 40f;
                    points.Add(new PointF(x, baseY + (float)(4 * Math.Sin(i * 0.5 + Now * (0.9 + k * 0.3) + k * 2))));
                }
                points.Add(new PointF(CardW, CardH));
                points.Add(new PointF(0, CardH));
                using (var brush = new SolidBrush(waveColors[k])) { g.FillPolygon(brush, points.ToArray()); }
            }
            // Warm specks drifting in the air
            foreach (var e in embers)
            {
                if (e.Y > CardH - 70) { continue; }
                int alpha = (int)(30 + 50 * (0.5 + 0.5 * Math.Sin(Now + e.Phase)));
                using (var brush = new SolidBrush(Color.FromArgb(alpha, 255, 230, 160)))
                {
                    g.FillEllipse(brush, e.X, e.Y, e.Size * 0.8f, e.Size * 0.8f);
                }
            }
        }

        /// Crimson Nebula: big soft clouds drifting in slow circles, and stars
        void DrawNebula(Graphics g)
        {
            var clouds = new[]
            {
                Color.FromArgb(70, 210, 20, 40), Color.FromArgb(50, 230, 40, 130), Color.FromArgb(44, 120, 30, 170),
            };
            for (int k = 0; k < 3; k++)
            {
                double t = Now * (0.16 + k * 0.05) + k * 2.1;
                float cx = CardW * (0.25f + 0.25f * k) + (float)(60 * Math.Cos(t));
                float cy = CardH * (0.3f + 0.18f * (k % 2)) + (float)(40 * Math.Sin(t * 1.3));
                DrawSoftLight(g, cx, cy, 230 - k * 20, 150 - k * 10, clouds[k]);
            }
            foreach (var e in embers)
            {
                float sx = e.X, sy = (e.Phase / 6.28f) * CardH;
                double twinkle = 0.5 + 0.5 * Math.Sin(Now * (1 + e.Drift * 0.1) + e.Phase * 3);
                using (var brush = new SolidBrush(Color.FromArgb((int)(40 + 150 * twinkle * twinkle), 255, 230, 236)))
                {
                    float r = 0.6f + e.Size * 0.35f;
                    g.FillEllipse(brush, sx - r, sy - r, r * 2, r * 2);
                }
            }
        }

        /// Lava Lamp: warm blobs rising slowly, swaying as they go
        void DrawLava(Graphics g)
        {
            DrawSoftLight(g, CardW / 2f, CardH + 40, 340, 140, Color.FromArgb(70, 255, 80, 20));
            for (int k = 0; k < 6; k++)
            {
                float size = 46 + (k * 37) % 50;
                double speed = 9 + (k * 5) % 9;
                float y = CardH + size - (float)((Now * speed + k * 97) % (CardH + size * 2));
                float x = CardW * ((k * 0.37f + 0.1f) % 1f) + (float)(30 * Math.Sin(Now * 0.4 + k * 1.7));
                float squash = (float)(1 + 0.12 * Math.Sin(Now * 0.9 + k));
                var color = k % 2 == 0 ? Color.FromArgb(255, 240, 40, 30) : Color.FromArgb(255, 255, 120, 40);
                DrawSoftLight(g, x, y, size * 1.4f, size * 1.4f * squash, Color.FromArgb(60, color));
                using (var brush = new SolidBrush(Color.FromArgb(46, color)))
                {
                    g.FillEllipse(brush, x - size / 2, y - size / 2 * squash, size, size * squash);
                }
            }
        }

        /// Red Grid: a glowing horizon and a grid floor moving toward you
        void DrawGrid(Graphics g)
        {
            float horizon = CardH * 0.6f;
            DrawSoftLight(g, CardW / 2f, horizon, 320, 110, Color.FromArgb(80, 255, 30, 60));
            using (var brush = new LinearGradientBrush(new RectangleF(0, horizon, CardW, CardH - horizon + 1),
                                                       Color.FromArgb(150, 8, 2, 6), Color.FromArgb(90, 40, 4, 14), 90f))
            {
                g.FillRectangle(brush, 0, horizon, CardW, CardH - horizon);
            }
            float depth = CardH - horizon;
            double roll = (Now * 0.35) % 1.0;
            for (int i = 0; i < 9; i++)
            {
                double p = (i + roll) / 9.0;
                float y = horizon + (float)(depth * p * p);
                using (var pen = new Pen(Color.FromArgb((int)(16 + 80 * p), 255, 40, 80), 1f)) { g.DrawLine(pen, 0, y, CardW, y); }
            }
            for (int i = -10; i <= 10; i++)
            {
                float bottomX = CardW / 2f + i * 60;
                using (var pen = new Pen(Color.FromArgb(48, 255, 40, 80), 1f)) { g.DrawLine(pen, CardW / 2f + i * 6, horizon, bottomX, CardH); }
            }
            using (var pen = new Pen(Color.FromArgb(120, 255, 90, 110), 1f)) { g.DrawLine(pen, 0, horizon, CardW, horizon); }
            foreach (var e in embers)
            {
                float sy = (e.Phase / 6.28f) * horizon * 0.9f;
                int alpha = (int)(30 + 60 * (0.5 + 0.5 * Math.Sin(Now * 1.3 + e.Phase)));
                using (var brush = new SolidBrush(Color.FromArgb(alpha, 255, 200, 210)))
                {
                    g.FillEllipse(brush, e.X, sy, 1.6f, 1.6f);
                }
            }
        }

        /// Red Silk: many thin lines flowing together like a ribbon of fabric
        void DrawSilk(Graphics g)
        {
            DrawSoftLight(g, CardW * 0.2f, CardH * 0.8f, 260, 140, Color.FromArgb(46, 200, 20, 40));
            for (int line = 0; line < 22; line++)
            {
                double offset = line * 0.09;
                var points = new PointF[33];
                for (int i = 0; i <= 32; i++)
                {
                    float x = (CardW + 40) * i / 32f - 20;
                    double u = i / 32.0;
                    float y = CardH * 0.72f
                              + (float)(46 * Math.Sin(u * 4.2 + Now * 0.45 + offset))
                              + (float)(22 * Math.Sin(u * 2.1 - Now * 0.3 + offset * 2))
                              + line * 2.6f - 28;
                    points[i] = new PointF(x, y);
                }
                int alpha = (int)(18 + 70 * Math.Sin(Math.PI * line / 21.0));
                using (var pen = new Pen(Color.FromArgb(alpha, 255, 70 + line * 3, 80), 1f)) { g.DrawCurve(pen, points); }
            }
        }

        /// Pulse: rings ripple out from the top like a heartbeat
        void DrawRipple(Graphics g)
        {
            var center = new PointF(CardW / 2f, 86);
            double beat = Math.Pow(0.5 + 0.5 * Math.Sin(Now * 2.4), 4);
            DrawSoftLight(g, center.X, center.Y, 150, 150, Color.FromArgb((int)(40 + 40 * beat), 220, 20, 34));
            for (int k = 0; k < 5; k++)
            {
                double life = (Now * 0.22 + k / 5.0) % 1.0;
                float r = 50 + (float)(life * 380);
                int alpha = (int)(90 * (1 - life) * (1 - life));
                using (var pen = new Pen(Color.FromArgb(alpha, 255, 60, 70), 1.4f))
                {
                    g.DrawEllipse(pen, center.X - r, center.Y - r, r * 2, r * 2);
                }
            }
        }

        /// Paper: a light card with a faint dot grid and a soft red corner
        void DrawPaper(Graphics g)
        {
            DrawCornerLight(g, CardW + 20, -30, 260, 34);
            DrawCornerLight(g, -40, CardH + 30, 220, 20);
            using (var brush = new SolidBrush(Color.FromArgb(22, 60, 30, 34)))
            {
                for (float y = 14; y < CardH; y += 18)
                {
                    for (float x = 14; x < CardW; x += 18) { g.FillEllipse(brush, x - 0.9f, y - 0.9f, 1.8f, 1.8f); }
                }
            }
        }

        static PointF[,] shardPoints;

        /// Shards: a jittered triangle mesh, each piece shimmering on its own
        void DrawShards(Graphics g)
        {
            const int cols = 9, rows = 6;
            if (shardPoints == null)
            {
                var random = new Random(7);
                shardPoints = new PointF[cols + 1, rows + 1];
                for (int x = 0; x <= cols; x++)
                {
                    for (int y = 0; y <= rows; y++)
                    {
                        float jx = (x == 0 || x == cols) ? 0 : (float)(random.NextDouble() - 0.5) * 40;
                        float jy = (y == 0 || y == rows) ? 0 : (float)(random.NextDouble() - 0.5) * 36;
                        shardPoints[x, y] = new PointF(CardW * x / (float)cols + jx, CardH * y / (float)rows + jy);
                    }
                }
            }
            int index = 0;
            for (int x = 0; x < cols; x++)
            {
                for (int y = 0; y < rows; y++)
                {
                    var a = shardPoints[x, y]; var b = shardPoints[x + 1, y]; var c = shardPoints[x, y + 1]; var d = shardPoints[x + 1, y + 1];
                    foreach (var tri in new[] { new[] { a, b, d }, new[] { a, d, c } })
                    {
                        // A slow light sweeps diagonally, and each shard shimmers a little on its own
                        float mx = (tri[0].X + tri[1].X + tri[2].X) / 3, my = (tri[0].Y + tri[1].Y + tri[2].Y) / 3;
                        double sweep = Math.Max(0, Math.Sin((mx + my) * 0.008 - Now * 0.8));
                        double shimmer = 0.5 + 0.5 * Math.Sin(Now * 0.9 + index * 1.7);
                        int alpha = (int)(8 + 22 * shimmer + 34 * Math.Pow(sweep, 4));
                        using (var brush = new SolidBrush(Color.FromArgb(alpha, Theme.Accent))) { g.FillPolygon(brush, tri); }
                        using (var pen = new Pen(Color.FromArgb(26, Theme.AccentBright), 1f)) { g.DrawPolygon(pen, tri); }
                        index++;
                    }
                }
            }
        }

        /// Chat: little message bubbles rising up the sides, like chat scrolling by
        void DrawChat(Graphics g)
        {
            DrawSoftLight(g, CardW / 2f, 90, 260, 150, Color.FromArgb(50, Theme.Accent));
            var names = new[] { Color.FromArgb(255, 255, 100, 120), Color.FromArgb(255, 90, 220, 160), Color.FromArgb(255, 255, 190, 80), Color.FromArgb(255, 120, 190, 255) };
            int i = 0;
            foreach (var e in embers)
            {
                i++;
                float w = 44 + e.Size * 14, h = 16;
                // Bubbles stay in the side columns so the middle stays clear
                float x = e.X < CardW / 2f ? 14 + (e.X / (CardW / 2f)) * 70 : CardW - 14 - w - ((CardW - e.X) / (CardW / 2f)) * 70;
                double fade = Math.Max(0, Math.Min(1, e.Y / 60.0)) * Math.Max(0, Math.Min(1, (CardH - e.Y) / 60.0));
                var r = new RectangleF(x, e.Y, w, h);
                using (var path = Rounded(r, 7))
                {
                    using (var fill = new SolidBrush(Color.FromArgb((int)(30 * fade), 255, 255, 255))) { g.FillPath(fill, path); }
                }
                using (var name = new SolidBrush(Color.FromArgb((int)(150 * fade), names[i % 4])))
                {
                    g.FillRectangle(name, r.X + 6, r.Y + 6, 10 + (i % 3) * 3, 4);
                }
                using (var line = new SolidBrush(Color.FromArgb((int)(70 * fade), 255, 255, 255)))
                {
                    g.FillRectangle(line, r.X + 22 + (i % 3) * 3, r.Y + 6, w - 30 - (i % 3) * 3, 4);
                }
            }
        }

        /// Neon: a dark brick wall with a red neon tube glowing around the edge
        void DrawNeon(Graphics g)
        {
            using (var pen = new Pen(Color.FromArgb(14, 255, 255, 255), 1f))
            {
                int row = 0;
                for (float y = 0; y < CardH; y += 16, row++)
                {
                    g.DrawLine(pen, 0, y, CardW, y);
                    for (float x = (row % 2) * 22; x < CardW; x += 44) { g.DrawLine(pen, x, y, x, y + 16); }
                }
            }
            // A gentle hum in the brightness, never a flash
            double hum = 0.9 + 0.1 * Math.Sin(Now * 3.1) * Math.Sin(Now * 1.7);
            DrawSoftLight(g, CardW / 2f, CardH / 2f, 330, 230, Color.FromArgb((int)(40 * hum), Theme.AccentDeep));
            var tube = new RectangleF(14, 14, CardW - 28, CardH - 28);
            using (var path = Rounded(tube, 12))
            {
                for (int i = 5; i >= 1; i--)
                {
                    using (var pen = new Pen(Color.FromArgb((int)(22 * hum), Theme.Accent), i * 3.2f)) { g.DrawPath(pen, path); }
                }
                using (var pen = new Pen(Color.FromArgb((int)(255 * hum), Theme.AccentBright), 1.6f)) { g.DrawPath(pen, path); }
            }
        }

        /// Spotlight: two stage lights sweeping slowly, with curtains at the sides
        void DrawSpotlight(Graphics g)
        {
            for (int k = 0; k < 2; k++)
            {
                float originX = k == 0 ? 60 : CardW - 60;
                double angle = (k == 0 ? 1 : -1) * (0.28 + 0.22 * Math.Sin(Now * 0.5 + k * 1.9));
                float reach = CardH + 40;
                float cx = originX + (float)Math.Sin(angle) * reach, cy = -10 + (float)Math.Cos(angle) * reach;
                float spread = 120;
                var cone = new[] { new PointF(originX - 8, -10), new PointF(originX + 8, -10), new PointF(cx + spread, cy), new PointF(cx - spread, cy) };
                using (var brush = new LinearGradientBrush(new RectangleF(0, -10, CardW, reach + 2), Color.FromArgb(46, 255, 230, 200), Color.FromArgb(0, 255, 230, 200), 90f))
                {
                    g.FillPolygon(brush, cone);
                }
                DrawSoftLight(g, cx, Math.Min(cy, CardH - 10), 110, 34, Color.FromArgb(40, 255, 220, 190));
            }
            // Velvet curtain folds down each side
            for (int side = 0; side < 2; side++)
            {
                for (int f = 0; f < 4; f++)
                {
                    float x = side == 0 ? f * 11 : CardW - (f + 1) * 11;
                    var fold = new RectangleF(x, 0, 11, CardH);
                    using (var brush = new LinearGradientBrush(new RectangleF(x - 0.5f, 0, 12, CardH), Color.FromArgb(150, 120, 6, 20), Color.FromArgb(150, 40, 2, 8), 0f))
                    {
                        g.FillRectangle(brush, fold);
                    }
                }
            }
            using (var brush = new SolidBrush(Color.FromArgb(170, 90, 4, 14))) { g.FillRectangle(brush, 0, 0, CardW, 8); }
        }

        /// Hex: a honeycomb with a slow wave of red light rolling through it
        void DrawHex(Graphics g)
        {
            const float size = 20;
            float w = size * 1.732f, h = size * 1.5f;
            int row = 0;
            for (float y = 0; y < CardH + size; y += h, row++)
            {
                for (float x = (row % 2) * w / 2; x < CardW + w; x += w)
                {
                    var hex = new PointF[6];
                    for (int i = 0; i < 6; i++)
                    {
                        double a = Math.PI / 3 * i + Math.PI / 6;
                        hex[i] = new PointF(x + (float)(Math.Cos(a) * (size - 2)), y + (float)(Math.Sin(a) * (size - 2)));
                    }
                    double wave = Math.Max(0, Math.Sin(x * 0.012 - y * 0.008 - Now * 1.1));
                    int fill = (int)(46 * Math.Pow(wave, 6));
                    if (fill > 1)
                    {
                        using (var brush = new SolidBrush(Color.FromArgb(fill, Theme.Accent))) { g.FillPolygon(brush, hex); }
                    }
                    using (var pen = new Pen(Color.FromArgb(18 + fill, Theme.AccentBright), 1f)) { g.DrawPolygon(pen, hex); }
                }
            }
        }

        /// Mono: a red stripe down the side and the name drifting huge behind
        void DrawMono(Graphics g)
        {
            using (var big = new Font("Segoe UI Black", 150f, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var brush = new SolidBrush(Color.FromArgb(12, 255, 255, 255)))
            {
                string word = "SOLRINO  ";
                float width = g.MeasureString(word, big).Width;
                float x = -(float)((Now * 18) % width);
                for (; x < CardW; x += width) { g.DrawString(word, big, brush, x, CardH - 190); }
            }
            using (var brush = new SolidBrush(Color.FromArgb(255, 227, 16, 28))) { g.FillRectangle(brush, 0, 0, 6, CardH); }
            using (var brush = new SolidBrush(Color.FromArgb(70, 227, 16, 28))) { g.FillRectangle(brush, 10, 0, 2, CardH); }
        }

        /// Bubblegum: soft bubbles floating up
        void DrawBubbles(Graphics g)
        {
            DrawCornerLight(g, 0, CardH, 300, 40);
            foreach (var e in embers)
            {
                float size = 6 + e.Size * 6;
                float x = e.X + (float)Math.Sin(Now * 0.7 + e.Phase) * e.Drift;
                var color = e.Phase > 3.14f ? Theme.Accent : Theme.AccentDeep;
                float middle = Math.Abs(x + size / 2 - CardW / 2f) / (CardW / 2f);
                double keep = Math.Max(0.15, Math.Min(1, (middle - 0.25) / 0.35));
                using (var fill = new SolidBrush(Color.FromArgb((int)(26 * keep), color)))
                using (var pen = new Pen(Color.FromArgb((int)(70 * keep), color), 1f))
                {
                    g.FillEllipse(fill, x, e.Y, size, size);
                    g.DrawEllipse(pen, x, e.Y, size, size);
                }
                using (var shine = new SolidBrush(Color.FromArgb((int)(60 * keep), 255, 255, 255)))
                {
                    g.FillEllipse(shine, x + size * 0.22f, e.Y + size * 0.2f, size * 0.22f, size * 0.22f);
                }
            }
        }

        // ------------------------------------------------------------ Pieces

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

        void DrawText(Graphics g, string text, Font font, Color color, float x, float y, float width = 0)
        {
            using (var brush = new SolidBrush(color))
            {
                if (width > 0) { g.DrawString(text, font, brush, new RectangleF(x, y, width, 400)); }
                else { g.DrawString(text, font, brush, x, y); }
            }
        }

        /// Text at a left-or-center spot, depending on the theme's layout
        void Line(Graphics g, string text, Font font, Color color, float y, float width = 0)
        {
            if (Theme.Left) { DrawText(g, text, font, color, Gutter, y, width > 0 ? width : CardW - Gutter * 2); }
            else { Centered(g, text, font, color, y); }
        }

        const float Gutter = 36f;

        /// The small logo and name at the top left, on left-aligned themes
        void DrawHeader(Graphics g, double alpha)
        {
            DrawLogo(g, Gutter + 18, 50, 40, alpha);
            DrawText(g, "Solrino", fontHeading, Fade(Theme.Text1, alpha), Gutter + 46, 37);
        }

        void DrawCredit(Graphics g, double alpha, float y)
        {
            Line(g, "Made by scary · a one-person dev team", fontSmall, Fade(Theme.Text2, alpha * 0.75), y);
        }

        void DrawCheckbox(Graphics g, double alpha, float x, float y)
        {
            string label = "Create a desktop shortcut";
            var size = g.MeasureString(label, fontBody);
            float total = 16 + 8 + size.Width;
            if (x < 0) { x = (CardW - total) / 2; }
            var hit = new RectangleF(x - 6, y - 5, total + 12, 27);
            bool hover = hit.Contains(mouse);
            var box = new RectangleF(x, y, 16, 16);
            using (var path = Rounded(box, 4))
            {
                if (desktopShortcut)
                {
                    using (var fill = new SolidBrush(Fade(Theme.Accent, alpha))) { g.FillPath(fill, path); }
                    using (var pen = new Pen(Fade(Theme.ButtonText, alpha), 1.8f))
                    {
                        pen.StartCap = LineCap.Round;
                        pen.EndCap = LineCap.Round;
                        g.DrawLines(pen, new[] { new PointF(x + 3.6f, y + 8.2f), new PointF(x + 6.8f, y + 11.2f), new PointF(x + 12.4f, y + 5f) });
                    }
                }
                else
                {
                    using (var pen = new Pen(Fade(hover ? Theme.Text1 : Theme.Text2, alpha), 1.3f)) { g.DrawPath(pen, path); }
                }
            }
            DrawText(g, label, fontBody, Fade(hover ? Theme.Text1 : Theme.Text2, alpha), x + 22, y - 2.5f);
            hotspots.Add(new Hotspot { Rect = hit, Click = delegate { desktopShortcut = !desktopShortcut; } });
        }

        void DrawButton(Graphics g, RectangleF r, string label, double alpha, Action click)
        {
            bool hover = r.Contains(mouse);
            bool pressed = hover && mouseDown;
            if (pressed) { r.Inflate(-1.5f, -1); }
            float radius = Theme.ButtonRadius < 0 ? r.Height / 2 : Theme.ButtonRadius;

            if (hover)
            {
                for (int i = 10; i > 0; i -= 2)
                {
                    var glow = r;
                    glow.Inflate(i, i);
                    using (var path = Rounded(glow, radius + i))
                    using (var brush = new SolidBrush(Fade(Color.FromArgb(14, Theme.AccentBright), alpha)))
                    {
                        g.FillPath(brush, path);
                    }
                }
            }

            using (var path = Rounded(r, radius))
            {
                using (var fill = new LinearGradientBrush(r, Fade(hover ? Theme.AccentBright : Theme.Accent, alpha), Fade(Theme.AccentDeep, alpha), Theme.ButtonAngle))
                {
                    g.FillPath(fill, path);
                }
                var state = g.Save();
                g.SetClip(path);
                double phase = (Now % 3.2) / 3.2;
                float sx = r.X - 80 + (float)(phase * (r.Width + 160));
                var band = new RectangleF(sx, r.Y, 80, r.Height);
                using (var shine = new LinearGradientBrush(band, Color.Transparent, Color.Transparent, 0f))
                {
                    var blend = new ColorBlend(3);
                    blend.Colors = new[] { Color.FromArgb(0, 255, 255, 255), Fade(Color.FromArgb(44, 255, 255, 255), alpha), Color.FromArgb(0, 255, 255, 255) };
                    blend.Positions = new[] { 0f, 0.5f, 1f };
                    shine.InterpolationColors = blend;
                    g.FillRectangle(shine, band);
                }
                g.Restore(state);
                using (var pen = new Pen(Fade(Color.FromArgb(90, Theme.AccentBright), alpha), 1f)) { g.DrawPath(pen, path); }
            }
            using (var brush = new SolidBrush(Fade(Theme.ButtonText, alpha)))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                format.LineAlignment = StringAlignment.Center;
                g.DrawString(label, fontButton, brush, r, format);
            }
            hotspots.Add(new Hotspot { Rect = r, Click = click });
        }

        void DrawSecondaryButton(Graphics g, RectangleF r, string label, double alpha, Action click)
        {
            bool hover = r.Contains(mouse);
            if (hover && mouseDown) { r.Inflate(-1.5f, -1); }
            float radius = Theme.ButtonRadius < 0 ? r.Height / 2 : Theme.ButtonRadius;
            using (var path = Rounded(r, radius))
            {
                using (var fill = new SolidBrush(Fade(Color.FromArgb(hover ? 34 : 16, Theme.Text1), alpha))) { g.FillPath(fill, path); }
                using (var pen = new Pen(Fade(Color.FromArgb(hover ? 120 : 70, Theme.Text1), alpha), 1f)) { g.DrawPath(pen, path); }
            }
            using (var brush = new SolidBrush(Fade(Theme.Text1, alpha)))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                format.LineAlignment = StringAlignment.Center;
                g.DrawString(label, fontButton, brush, r, format);
            }
            hotspots.Add(new Hotspot { Rect = r, Click = click });
        }

        void DrawLink(Graphics g, string label, float y, double alpha, Action click, float at = -1)
        {
            var size = g.MeasureString(label, fontSmall);
            float x = at >= 0 ? at : Theme.Left ? Gutter : (CardW - size.Width) / 2;
            var r = new RectangleF(x, y, size.Width, size.Height);
            bool hover = r.Contains(mouse);
            DrawText(g, label, fontSmall, Fade(hover ? Theme.Text1 : Theme.AccentBright, alpha), r.X, r.Y);
            hotspots.Add(new Hotspot { Rect = r, Click = click });
        }

        void DrawProgress(Graphics g, RectangleF track, double value)
        {
            value = Math.Max(0, Math.Min(1, value));
            switch (Theme.Progress)
            {
                case Bar.Thin:
                {
                    var line = new RectangleF(track.X, track.Y + track.Height / 2 - 2, track.Width, 4);
                    using (var path = Rounded(line, 2))
                    using (var brush = new SolidBrush(Color.FromArgb(40, Theme.Text1))) { g.FillPath(brush, path); }
                    var bar = new RectangleF(line.X, line.Y, Math.Max(4, line.Width * (float)value), 4);
                    using (var path = Rounded(bar, 2))
                    using (var brush = new SolidBrush(Theme.AccentBright)) { g.FillPath(brush, path); }
                    break;
                }
                case Bar.Segments:
                {
                    const int count = 24;
                    float gap = 3, w = (track.Width - gap * (count - 1)) / count;
                    var seg = new RectangleF(0, track.Y + track.Height / 2 - 5, w, 10);
                    for (int i = 0; i < count; i++)
                    {
                        seg.X = track.X + i * (w + gap);
                        double fill = Math.Max(0, Math.Min(1, value * count - i));
                        using (var path = Rounded(seg, 2))
                        {
                            using (var brush = new SolidBrush(Color.FromArgb(30, Theme.Accent))) { g.FillPath(brush, path); }
                            if (fill > 0)
                            {
                                using (var brush = new SolidBrush(Color.FromArgb((int)(255 * fill), Theme.AccentBright))) { g.FillPath(brush, path); }
                            }
                        }
                    }
                    break;
                }
                case Bar.Chunky:
                {
                    var big = new RectangleF(track.X, track.Y + track.Height / 2 - 9, track.Width, 18);
                    using (var path = Rounded(big, 9))
                    using (var brush = new SolidBrush(Color.FromArgb(50, 0, 0, 0))) { g.FillPath(brush, path); }
                    var bar = new RectangleF(big.X, big.Y, Math.Max(18, big.Width * (float)value), big.Height);
                    using (var path = Rounded(bar, 9))
                    {
                        using (var fill = new LinearGradientBrush(new RectangleF(big.X, big.Y, big.Width + 1, big.Height), Theme.AccentDeep, Theme.Accent, 0f))
                        {
                            g.FillPath(fill, path);
                        }
                        var state = g.Save();
                        g.SetClip(path);
                        float offset = (float)((Now * 22) % 16);
                        using (var pen = new Pen(Color.FromArgb(46, 255, 255, 255), 6f))
                        {
                            for (float x = bar.X - 20 + offset; x < bar.Right + 20; x += 16)
                            {
                                g.DrawLine(pen, x, bar.Bottom, x + 12, bar.Y);
                            }
                        }
                        g.Restore(state);
                    }
                    break;
                }
                default:
                {
                    using (var path = Rounded(track, track.Height / 2))
                    using (var brush = new SolidBrush(Theme.Light ? Color.FromArgb(34, Theme.Text1) : Color.FromArgb(150, 0, 0, 0))) { g.FillPath(brush, path); }
                    float width = Math.Max(track.Height, (float)(track.Width * value));
                    var bar = new RectangleF(track.X, track.Y, width, track.Height);
                    for (int i = 8; i > 0; i -= 2)
                    {
                        var glow = bar;
                        glow.Inflate(i, i);
                        using (var path = Rounded(glow, track.Height / 2 + i))
                        using (var brush = new SolidBrush(Color.FromArgb(13, Theme.AccentBright))) { g.FillPath(brush, path); }
                    }
                    using (var path = Rounded(bar, track.Height / 2))
                    using (var fill = new LinearGradientBrush(new RectangleF(track.X, track.Y, track.Width + 1, track.Height), Theme.AccentDeep, Theme.AccentBright, 0f))
                    {
                        g.FillPath(fill, path);
                    }
                    break;
                }
            }
        }

        void DrawClose(Graphics g)
        {
            var r = new RectangleF(CardW - 42, 12, 30, 30);
            bool hover = r.Contains(mouse);
            if (hover)
            {
                using (var path = Rounded(r, 8))
                using (var brush = new SolidBrush(Theme.Light ? Color.FromArgb(20, Theme.Text1) : Color.FromArgb(50, 255, 255, 255)))
                {
                    g.FillPath(brush, path);
                }
            }
            using (var pen = new Pen(hover ? Theme.Text1 : Theme.Text2, 1.6f))
            {
                pen.StartCap = LineCap.Round;
                pen.EndCap = LineCap.Round;
                g.DrawLine(pen, r.X + 10, r.Y + 10, r.Right - 10, r.Bottom - 10);
                g.DrawLine(pen, r.Right - 10, r.Y + 10, r.X + 10, r.Bottom - 10);
            }
            hotspots.Add(new Hotspot { Rect = r, Click = delegate { timer.Stop(); Close(); } });
        }

        // ------------------------------------------------------------ Pages

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

        /// The welcome screen's top part, different for each theme
        void DrawWelcomeTop(Graphics g, double a, double b)
        {
            switch (Theme.Background)
            {
                case Backdrop.Gold:
                    Centered(g, "WELCOME TO", fontEyebrow, Fade(Theme.AccentBright, b), 38);
                    DrawLogo(g, CardW / 2f, 94 + (float)((1 - a) * 14), 70, a);
                    Centered(g, "Solrino", fontTitle, Fade(Theme.Text1, b), 132);
                    using (var pen = new Pen(Fade(Color.FromArgb(150, Theme.Accent), b), 1f))
                    {
                        g.DrawLine(pen, CardW / 2f - 60, 180, CardW / 2f - 8, 180);
                        g.DrawLine(pen, CardW / 2f + 8, 180, CardW / 2f + 60, 180);
                    }
                    using (var brush = new SolidBrush(Fade(Theme.Accent, b)))
                    {
                        g.FillPolygon(brush, new[] { new PointF(CardW / 2f, 176), new PointF(CardW / 2f + 4, 180), new PointF(CardW / 2f, 184), new PointF(CardW / 2f - 4, 180) });
                    }
                    Centered(g, "Twitch chat, made yours.", fontBody, Fade(Theme.Text2, b), 190);
                    break;
                case Backdrop.Bubbles:
                {
                    float bob = (float)(4 * Math.Sin(Now * 1.8));
                    DrawLogo(g, CardW / 2f, 86 + bob + (float)((1 - a) * 20), 96, a);
                    Centered(g, "Solrino", fontTitle, Fade(Theme.Text1, b), 144);
                    Centered(g, Theme.Tagline, fontBody, Fade(Theme.Text2, b), 182);
                    break;
                }
                default:
                {
                    // Aurora: the logo sits in a soft halo under the lights
                    using (var path = new GraphicsPath())
                    {
                        path.AddEllipse(CardW / 2f - 80, 6, 160, 160);
                        using (var brush = new PathGradientBrush(path))
                        {
                            brush.CenterColor = Fade(Color.FromArgb(70, Theme.Accent), a);
                            brush.SurroundColors = new[] { Color.FromArgb(0, Theme.Accent) };
                            g.FillPath(brush, path);
                        }
                    }
                    DrawLogo(g, CardW / 2f, 86 + (float)((1 - a) * 20), 92, a);
                    Centered(g, "Solrino", fontTitle, Fade(Theme.Text1, b), 144);
                    Centered(g, Theme.Tagline, fontBody, Fade(Theme.Text2, b), 182);
                    break;
                }
            }
        }

        void DrawWelcome(Graphics g)
        {
            double t = Now - screenStart;
            double a = EaseOut(t / 0.6);
            double b = EaseOut((t - 0.15) / 0.6);
            double c = EaseOut((t - 0.35) / 0.5);

            if (Theme.Left)
            {
                float slide = (float)((1 - b) * 14);
                DrawHeader(g, a);
                DrawText(g, "Install Solrino", fontHero, Fade(Theme.Text1, b), Gutter - 2 + slide, 102);
                DrawText(g, "Twitch chat, made yours. It only takes a few seconds, and you can start chatting right after.",
                     fontBody, Fade(Theme.Text2, b), Gutter + 1 + slide, 152, CardW - 90);
                DrawCheckbox(g, c, Gutter + 2, 214);
                using (var pen = new Pen(Fade(Color.FromArgb(26, Theme.Text1), c), 1f))
                {
                    g.DrawLine(pen, Gutter + 2, 250, CardW - Gutter - 2, 250);
                }
                DrawButton(g, new RectangleF(CardW - Gutter - 182, 268, 180, 44), "Install", c, delegate { BeginInstall(); });
                DrawText(g, "Made by scary · a one-person dev team", fontSmall, Fade(Theme.Text2, c * 0.75), Gutter + 2, 282);
                return;
            }

            DrawWelcomeTop(g, a, b);
            DrawCheckbox(g, c, -1, 216);
            DrawButton(g, new RectangleF(CardW / 2f - 112, 246, 224, 46), "Install Solrino", c, delegate { BeginInstall(); });
            DrawCredit(g, c, 304);
        }

        void DrawImport(Graphics g)
        {
            double t = Now - screenStart;
            double a = EaseOut(t / 0.6);
            double text = EaseOut((t - 0.15) / 0.45);
            double controls = EaseOut((t - 0.3) / 0.45);

            float rowsX, rowsW, top;
            if (Theme.Left)
            {
                DrawHeader(g, a);
                DrawText(g, "Bring your settings over?", fontTitle, Fade(Theme.Text1, text), Gutter - 1, 80);
                DrawText(g, "Import your settings and chat tabs from another fork such as Moltorino or Bluzyrino.",
                     fontBody, Fade(Theme.Text2, text), Gutter + 1, 118, CardW - Gutter * 2);
                rowsX = Gutter; rowsW = CardW - Gutter * 2; top = 166;
            }
            else
            {
                DrawLogo(g, CardW / 2f, 46 + (float)((1 - a) * 20), 52, a);
                Centered(g, "Bring your settings over?", fontHeading, Fade(Theme.Text1, text), 76);
                using (var brush = new SolidBrush(Fade(Theme.Text2, text)))
                using (var format = new StringFormat())
                {
                    format.Alignment = StringAlignment.Center;
                    g.DrawString("Import your settings and chat tabs from another fork such as Moltorino or Bluzyrino.",
                        fontBody, brush, new RectangleF(60, 104, CardW - 120, 40), format);
                }
                rowsX = 70; rowsW = CardW - 140; top = 148;
            }

            float rowHeight = 30f;
            for (int i = 0; i < forks.Count; i++)
            {
                DrawForkRow(g, new RectangleF(rowsX, top + i * rowHeight, rowsW, rowHeight - 4), i, controls);
            }
            DrawLink(g, "Choose another folder", top + Math.Max(1, forks.Count) * rowHeight + 2, controls, delegate { PickImportFolder(); });

            float buttonsY = 268;
            if (Theme.Left)
            {
                DrawSecondaryButton(g, new RectangleF(CardW - Gutter - 300, buttonsY, 140, 42), "Start fresh", controls, delegate { AnswerImport(""); });
                DrawButton(g, new RectangleF(CardW - Gutter - 150, buttonsY, 150, 42), "Import", controls, delegate { AnswerImport(forks[chosenFork].Folder); });
                DrawText(g, "Your logins come too.", fontSmall, Fade(Theme.Text2, controls * 0.7), Gutter, buttonsY + 13);
            }
            else
            {
                DrawSecondaryButton(g, new RectangleF(CardW / 2f - 166, buttonsY - 6, 158, 42), "Start fresh", controls, delegate { AnswerImport(""); });
                DrawButton(g, new RectangleF(CardW / 2f + 8, buttonsY - 6, 158, 42), "Import", controls, delegate { AnswerImport(forks[chosenFork].Folder); });
                Centered(g, "Your logins come too. The other app isn't changed.", fontSmall, Fade(Theme.Text2, controls * 0.7), 312);
            }
        }

        void DrawForkRow(Graphics g, RectangleF r, int index, double alpha)
        {
            bool chosen = index == chosenFork;
            bool hover = r.Contains(mouse);
            float radius = Theme.ButtonRadius < 0 ? r.Height / 2 : 8;
            using (var path = Rounded(r, radius))
            {
                if (chosen || hover)
                {
                    using (var brush = new SolidBrush(Fade(chosen ? Color.FromArgb(58, Theme.Accent) : Color.FromArgb(22, Theme.Text1), alpha)))
                    {
                        g.FillPath(brush, path);
                    }
                }
                using (var pen = new Pen(Fade(chosen ? Color.FromArgb(190, Theme.AccentBright) : Color.FromArgb(46, Theme.Text1), alpha), 1f))
                {
                    g.DrawPath(pen, path);
                }
            }

            var dot = new RectangleF(r.X + 10, r.Y + r.Height / 2 - 7, 14, 14);
            using (var pen = new Pen(Fade(chosen ? Theme.AccentBright : Theme.Text2, alpha), 1.4f)) { g.DrawEllipse(pen, dot); }
            if (chosen)
            {
                using (var brush = new SolidBrush(Fade(Theme.AccentBright, alpha)))
                {
                    g.FillEllipse(brush, dot.X + 3.5f, dot.Y + 3.5f, 7, 7);
                }
            }

            using (var format = new StringFormat())
            {
                format.LineAlignment = StringAlignment.Center;
                format.Trimming = StringTrimming.EllipsisCharacter;
                format.FormatFlags = StringFormatFlags.NoWrap;
                using (var brush = new SolidBrush(Fade(Theme.Text1, alpha)))
                {
                    g.DrawString(forks[index].Name, fontBody, brush, new RectangleF(r.X + 32, r.Y, r.Width - 160, r.Height), format);
                }
                format.Alignment = StringAlignment.Far;
                using (var brush = new SolidBrush(Fade(Theme.Text2, alpha)))
                {
                    g.DrawString(forks[index].LastUsed, fontSmall, brush, new RectangleF(r.Right - 138, r.Y, 128, r.Height), format);
                }
            }
            hotspots.Add(new Hotspot { Rect = r, Click = delegate { chosenFork = index; } });
        }

        string CurrentFileText()
        {
            string file = installer != null ? installer.CurrentFile : previewFile;
            if (file.Length > 62) { file = "..." + file.Substring(file.Length - 59); }
            return file;
        }

        void DrawInstalling(Graphics g)
        {
            string percent = (int)Math.Round(shownProgress * 100) + "%";
            if (Theme.Left)
            {
                DrawHeader(g, 1);
                DrawText(g, "Installing", fontTitle, Theme.Text1, Gutter - 1, 96);
                using (var brush = new SolidBrush(Theme.AccentBright))
                using (var format = new StringFormat())
                {
                    format.Alignment = StringAlignment.Far;
                    g.DrawString(percent, fontPercent, brush, new RectangleF(CardW / 2f, 78, CardW / 2f - Gutter, 60), format);
                }
                DrawProgress(g, new RectangleF(Gutter, 160, CardW - Gutter * 2, 12), shownProgress);
                DrawText(g, CurrentFileText(), fontSmall, Theme.Text2, Gutter, 186);
                DrawText(g, "This only takes a moment", fontSmall, Fade(Theme.Text2, 0.7), Gutter, 296);
                return;
            }

            double pulse = 1 + 0.025 * Math.Sin(Now * 3);
            DrawLogo(g, CardW / 2f, 76, (float)(80 * pulse), 1);
            Centered(g, "Installing Solrino", fontHeading, Theme.Text1, 126);
            Centered(g, percent, fontPercent, Theme.AccentBright, 152);
            DrawProgress(g, new RectangleF(44, 232, CardW - 88, 12), shownProgress);
            Centered(g, CurrentFileText(), fontSmall, Theme.Text2, 260);
            Centered(g, "This only takes a moment", fontSmall, Fade(Theme.Text2, 0.7), 296);
        }

        /// A circle with a check mark that draws itself in
        void DrawCheckMark(Graphics g, PointF c, float radius, double t)
        {
            using (var brush = new SolidBrush(Fade(Color.FromArgb(40, Theme.Accent), t)))
            {
                g.FillEllipse(brush, c.X - radius, c.Y - radius, radius * 2, radius * 2);
            }
            using (var pen = new Pen(Fade(Theme.AccentBright, t), 2.4f))
            {
                g.DrawEllipse(pen, c.X - radius, c.Y - radius, radius * 2, radius * 2);
                pen.StartCap = LineCap.Round;
                pen.EndCap = LineCap.Round;
                pen.LineJoin = LineJoin.Round;
                var p1 = new PointF(c.X - radius * 0.42f, c.Y + radius * 0.02f);
                var p2 = new PointF(c.X - radius * 0.1f, c.Y + radius * 0.34f);
                var p3 = new PointF(c.X + radius * 0.46f, c.Y - radius * 0.32f);
                double draw = Math.Max(0, Math.Min(1, t * 1.3));
                if (draw < 0.5)
                {
                    float k = (float)(draw / 0.5);
                    g.DrawLine(pen, p1, new PointF(p1.X + (p2.X - p1.X) * k, p1.Y + (p2.Y - p1.Y) * k));
                }
                else
                {
                    float k = (float)((draw - 0.5) / 0.5);
                    g.DrawLines(pen, new[] { p1, p2, new PointF(p2.X + (p3.X - p2.X) * k, p2.Y + (p3.Y - p2.Y) * k) });
                }
            }
        }

        void DrawDone(Graphics g)
        {
            double t = EaseOut((Now - screenStart) / 0.6);
            string next = options.Launch ? "Opening Solrino..." : "Solrino is installed";
            if (Theme.Left)
            {
                DrawHeader(g, 1);
                DrawCheckMark(g, new PointF(Gutter + 26, 128), 24, t);
                DrawText(g, "You're all set", fontHero, Fade(Theme.Text1, t), Gutter + 64, 106);
                DrawText(g, next, fontBody, Fade(Theme.Text2, t), Gutter + 2, 186);
                DrawProgress(g, new RectangleF(Gutter, 290, CardW - Gutter * 2, 8), 1);
                return;
            }
            DrawCheckMark(g, new PointF(CardW / 2f, 104), 40, t);
            Centered(g, "You're all set", fontTitle, Fade(Theme.Text1, t), 166);
            Centered(g, next, fontBody, Fade(Theme.Text2, t), 206);
            DrawProgress(g, new RectangleF(44, 270, CardW - 88, 8), 1);
        }

        void DrawProblem(Graphics g)
        {
            double t = EaseOut((Now - screenStart) / 0.35);
            if (Theme.Left)
            {
                DrawHeader(g, t);
                DrawText(g, "That didn't finish", fontTitle, Fade(Theme.Text1, t), Gutter - 1, 96);
                DrawText(g, problem, fontBody, Fade(Theme.Text2, t), Gutter + 1, 140, CardW - Gutter * 2);
                DrawButton(g, new RectangleF(CardW - Gutter - 182, 268, 180, 44), "Try again", t, delegate { BeginInstall(); });
                return;
            }
            DrawLogo(g, CardW / 2f, 70, 64, t);
            Centered(g, "That didn't finish", fontHeading, Fade(Theme.Text1, t), 112);
            using (var brush = new SolidBrush(Fade(Theme.Text2, t)))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                format.Trimming = StringTrimming.EllipsisWord;
                g.DrawString(problem, fontBody, brush, new RectangleF(44, 146, CardW - 88, 94), format);
            }
            DrawButton(g, new RectangleF(CardW / 2f - 92, 256, 184, 44), "Try again", t, delegate { BeginInstall(); });
        }

        // ------------------------------------------------------------ Layouts

        /// Where a page puts its content, for the layouts beyond the classic two
        class FlowSpec
        {
            public RectangleF Area;
            /// Show the small logo and name above the content
            public bool Header;
            /// Buttons and notes sit in a bar along the bottom of the card
            public bool Bar;
            /// Show the "Made by scary" line on the welcome page
            public bool Credit = true;
            public string WelcomeTitle = "Install Solrino";
            public bool Narrow { get { return Area.Width < 400; } }
        }

        static Color Mix(Color a, Color b, float t)
        {
            return Color.FromArgb(255, (int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
        }

        void DrawArranged(Graphics g)
        {
            double a = EaseOut((Now - screenStart) / 0.5);
            var spec = new FlowSpec();
            var card = new RectangleF(0, 0, CardW, CardH);
            var state = g.Save();
            using (var clip = Rounded(card, Radius)) { g.SetClip(clip); }
            switch (Theme.Arrangement)
            {
                case Arrange.Split:
                    DrawSplitPanel(g);
                    spec.Area = new RectangleF(224, 24, CardW - 224 - 26, CardH - 48);
                    spec.Credit = false;
                    break;
                case Arrange.Banner:
                    DrawBanner(g);
                    spec.Area = new RectangleF(Gutter, 110, CardW - Gutter * 2, CardH - 110 - 20);
                    spec.WelcomeTitle = "Ready when you are";
                    break;
                case Arrange.Steps:
                    DrawStepsRail(g);
                    spec.Area = new RectangleF(200, 24, CardW - 200 - 26, CardH - 48);
                    spec.Credit = false;
                    spec.WelcomeTitle = "Get ready";
                    break;
                case Arrange.Bar:
                    DrawBottomBar(g);
                    spec.Area = new RectangleF(Gutter, 22, CardW - Gutter * 2, CardH - 64 - 22 - 14);
                    spec.Header = true;
                    spec.Bar = true;
                    break;
                case Arrange.Poster:
                    DrawPosterLogo(g);
                    // Mirrored, the content starts past the logo so the words never sit on it
                    spec.Area = Theme.PosterLeft ? new RectangleF(204, 22, CardW - 204 - 30, CardH - 46)
                                                 : new RectangleF(Gutter, 22, 330, CardH - 46);
                    spec.Header = !Theme.PosterLeft;
                    break;
                case Arrange.Inset:
                    DrawInsetPanel(g);
                    spec.Area = new RectangleF(76, 52, CardW - 152, CardH - 104);
                    break;
            }
            g.Restore(state);
            if (spec.Header) { DrawHeader(g, a); }

            switch (screen)
            {
                case Screen.Welcome: FlowWelcome(g, spec); break;
                case Screen.Import: FlowImport(g, spec); break;
                case Screen.Installing: FlowInstalling(g, spec); break;
                case Screen.Done: FlowDone(g, spec); break;
                case Screen.Problem: FlowProblem(g, spec); break;
            }
        }

        // ---- Layout decorations

        /// Split: a colored panel on the left holding the logo and name
        void DrawSplitPanel(Graphics g)
        {
            double a = EaseOut((Now - screenStart) / 0.6);
            var panel = new RectangleF(0, 0, 198, CardH);
            using (var brush = new LinearGradientBrush(panel, Mix(Theme.AccentDeep, Color.Black, 0.25f), Mix(Theme.AccentDeep, Color.Black, 0.8f), 70f))
            {
                g.FillRectangle(brush, panel);
            }
            var state = g.Save();
            g.SetClip(panel, CombineMode.Intersect);
            DrawSoftLight(g, 99, 96, 170, 130, Color.FromArgb(80, Theme.AccentBright));
            using (var pen = new Pen(Color.FromArgb(18, 255, 255, 255), 1f))
            {
                for (int i = 0; i < 4; i++)
                {
                    float r = 60 + i * 34 + (float)(6 * Math.Sin(Now * 0.8 + i));
                    g.DrawEllipse(pen, -40 - r, CardH + 20 - r, r * 2, r * 2);
                }
            }
            g.Restore(state);
            using (var pen = new Pen(Color.FromArgb(80, Theme.AccentBright), 1f)) { g.DrawLine(pen, 198, 0, 198, CardH); }

            DrawLogo(g, 99, 108 + (float)((1 - a) * 14), 92, a);
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Center;
                using (var brush = new SolidBrush(Fade(Color.White, a)))
                {
                    g.DrawString("Solrino", fontTitle, brush, new RectangleF(0, 162, 198, 40), format);
                }
                using (var brush = new SolidBrush(Fade(Color.FromArgb(200, 255, 255, 255), a)))
                {
                    g.DrawString(Theme.Tagline, fontSmall, brush, new RectangleF(20, 200, 158, 40), format);
                }
                using (var brush = new SolidBrush(Fade(Color.FromArgb(130, 255, 255, 255), a)))
                {
                    g.DrawString("Made by scary", fontSmall, brush, new RectangleF(0, CardH - 34, 198, 20), format);
                }
            }
        }

        /// Banner: a bold strip across the top with the logo on a badge
        void DrawBanner(Graphics g)
        {
            double a = EaseOut((Now - screenStart) / 0.6);
            var band = new RectangleF(0, 0, CardW, 92);
            using (var brush = new LinearGradientBrush(new RectangleF(0, 0, CardW + 1, 92), Theme.Accent, Theme.AccentDeep, 0f))
            {
                g.FillRectangle(brush, band);
            }
            using (var brush = new SolidBrush(Color.FromArgb(22, 255, 255, 255)))
            {
                float drift = (float)(8 * Math.Sin(Now * 0.6));
                g.FillEllipse(brush, CardW - 150 + drift, -50, 130, 130);
                g.FillEllipse(brush, CardW - 230 - drift, 46, 70, 70);
            }
            using (var brush = new LinearGradientBrush(new RectangleF(0, 92, CardW, 10), Color.FromArgb(60, 0, 0, 0), Color.FromArgb(0, 0, 0, 0), 90f))
            {
                g.FillRectangle(brush, 0, 92, CardW, 10);
            }
            using (var brush = new SolidBrush(Fade(Color.FromArgb(245, 255, 255, 255), a)))
            {
                g.FillEllipse(brush, Gutter, 18, 56, 56);
            }
            DrawLogo(g, Gutter + 28, 46, 40, a);
            DrawText(g, "Solrino", fontTitle, Fade(Color.White, a), Gutter + 68, 18);
            DrawText(g, Theme.Tagline, fontSmall, Fade(Color.FromArgb(215, 255, 255, 255), a), Gutter + 71, 54);
        }

        /// Steps: a rail on the left listing where you are in the setup
        void DrawStepsRail(Graphics g)
        {
            var rail = new RectangleF(0, 0, 176, CardH);
            using (var brush = new SolidBrush(Theme.Light ? Color.FromArgb(14, Theme.Accent) : Color.FromArgb(70, 0, 0, 0))) { g.FillRectangle(brush, rail); }
            using (var pen = new Pen(Color.FromArgb(28, Theme.Text1), 1f)) { g.DrawLine(pen, 176, 0, 176, CardH); }
            DrawLogo(g, 40, 44, 34, 1);
            DrawText(g, "Solrino", fontHeading, Theme.Text1, 62, 31);

            var steps = new List<string>();
            bool settings = forks.Count > 0;
            if (settings) { steps.Add("Bring settings"); }
            steps.Add("Get ready");
            steps.Add("Install");
            steps.Add("All done");
            int offset = settings ? 1 : 0;
            int current = screen == Screen.Import ? 0
                        : screen == Screen.Welcome ? offset
                        : screen == Screen.Done ? offset + 2
                        : offset + 1;

            for (int i = 0; i < steps.Count; i++)
            {
                float cy = 104 + i * 46;
                var c = new PointF(40, cy);
                bool done = i < current, now = i == current;
                if (i < steps.Count - 1)
                {
                    using (var pen = new Pen(done ? Color.FromArgb(200, Theme.Accent) : Color.FromArgb(40, Theme.Text1), 2f))
                    {
                        g.DrawLine(pen, c.X, cy + 14, c.X, cy + 32);
                    }
                }
                var dot = new RectangleF(c.X - 11, cy - 11, 22, 22);
                if (done)
                {
                    using (var brush = new SolidBrush(Theme.Accent)) { g.FillEllipse(brush, dot); }
                    using (var pen = new Pen(Theme.ButtonText, 1.8f))
                    {
                        pen.StartCap = LineCap.Round; pen.EndCap = LineCap.Round;
                        g.DrawLines(pen, new[] { new PointF(c.X - 4.5f, cy), new PointF(c.X - 1, cy + 3.5f), new PointF(c.X + 5, cy - 3.5f) });
                    }
                }
                else
                {
                    double glow = now ? 0.6 + 0.4 * Math.Sin(Now * 3) : 0;
                    if (now)
                    {
                        using (var brush = new SolidBrush(Color.FromArgb((int)(40 + 30 * glow), Theme.Accent))) { g.FillEllipse(brush, dot); }
                    }
                    using (var pen = new Pen(now ? Theme.AccentBright : Color.FromArgb(110, Theme.Text2), now ? 1.8f : 1.2f)) { g.DrawEllipse(pen, dot); }
                    using (var brush = new SolidBrush(now ? Theme.Text1 : Theme.Text2))
                    using (var format = new StringFormat())
                    {
                        format.Alignment = StringAlignment.Center;
                        format.LineAlignment = StringAlignment.Center;
                        g.DrawString((i + 1).ToString(), fontSmall, brush, new RectangleF(dot.X, dot.Y + 0.5f, dot.Width, dot.Height), format);
                    }
                }
                DrawText(g, steps[i], now ? fontButton : fontBody, now ? Theme.Text1 : Theme.Text2, 62, cy - (now ? 10 : 9));
            }
            DrawText(g, "Made by scary", fontSmall, Fade(Theme.Text2, 0.7), 24, CardH - 34);
        }

        /// Bar: a darker strip along the bottom that holds the buttons
        void DrawBottomBar(Graphics g)
        {
            var strip = new RectangleF(0, CardH - 64, CardW, 64);
            using (var brush = new SolidBrush(Theme.Light ? Color.FromArgb(12, Theme.Text1) : Color.FromArgb(80, 0, 0, 0))) { g.FillRectangle(brush, strip); }
            using (var pen = new Pen(Color.FromArgb(26, Theme.Text1), 1f)) { g.DrawLine(pen, 0, strip.Y, CardW, strip.Y); }
            DrawCornerLight(g, CardW - 30, -10, 240, 34);
        }

        /// Poster: the logo huge and faded on the right, with content on the left
        void DrawPosterLogo(Graphics g)
        {
            float cx = Theme.PosterX, cy = Theme.PosterY;
            if (Theme.PosterSlash)
            {
                // A wide diagonal band of color behind the logo
                var band = new[] { new PointF(cx - 150, CardH + 10), new PointF(cx + 10, -10), new PointF(cx + 150, -10), new PointF(cx - 10, CardH + 10) };
                using (var brush = new LinearGradientBrush(new RectangleF(cx - 150, -10, 300, CardH + 20), Color.FromArgb(150, Theme.Accent), Color.FromArgb(60, Theme.AccentDeep), 90f))
                {
                    g.FillPolygon(brush, band);
                }
                using (var pen = new Pen(Color.FromArgb(120, Theme.AccentBright), 1.4f))
                {
                    g.DrawLine(pen, cx - 176, CardH + 10, cx - 16, -10);
                }
            }
            DrawSoftLight(g, cx, cy, Theme.PosterSize * 0.8f, Theme.PosterSize * 0.66f,
                          Color.FromArgb((int)(46 + 14 * Math.Sin(Now * 1.2)), Theme.Accent));
            if (Theme.PosterRays)
            {
                // Soft beams turning slowly around the logo
                var state = g.Save();
                g.TranslateTransform(cx, cy);
                g.RotateTransform((float)(Now * 4));
                using (var brush = new SolidBrush(Color.FromArgb(16, Theme.AccentBright)))
                {
                    for (int i = 0; i < 12; i++)
                    {
                        double a1 = Math.PI * 2 * i / 12, a2 = a1 + 0.16;
                        g.FillPolygon(brush, new[]
                        {
                            new PointF(0, 0),
                            new PointF((float)Math.Cos(a1) * 600, (float)Math.Sin(a1) * 600),
                            new PointF((float)Math.Cos(a2) * 600, (float)Math.Sin(a2) * 600),
                        });
                    }
                }
                g.Restore(state);
            }
            float bob = Theme.PosterSpin == 0 ? (float)(5 * Math.Sin(Now * 0.9)) : 0;
            if (Theme.PosterSpin != 0)
            {
                var state = g.Save();
                g.TranslateTransform(cx, cy);
                g.RotateTransform((float)(Now * Theme.PosterSpin));
                DrawLogo(g, 0, 0, Theme.PosterSize, Theme.PosterAlpha);
                g.Restore(state);
            }
            else
            {
                DrawLogo(g, cx, cy + bob, Theme.PosterSize, Theme.PosterAlpha);
            }
        }

        /// Inset: a floating inner card that holds everything
        void DrawInsetPanel(Graphics g)
        {
            var panel = new RectangleF(52, 30, CardW - 104, CardH - 60);
            for (int i = 12; i > 0; i -= 3)
            {
                var shade = panel;
                shade.Inflate(i, i);
                shade.Offset(0, 4);
                using (var path = Rounded(shade, 14 + i))
                using (var brush = new SolidBrush(Color.FromArgb(10, 0, 0, 0))) { g.FillPath(brush, path); }
            }
            using (var path = Rounded(panel, 14))
            {
                using (var brush = new SolidBrush(Color.FromArgb(140, Theme.InkBottom))) { g.FillPath(brush, path); }
                using (var pen = new Pen(Color.FromArgb(44, Theme.Text1), 1f)) { g.DrawPath(pen, path); }
            }
            using (var pen = new Pen(Color.FromArgb(90, Theme.AccentBright), 1.4f))
            {
                g.DrawLine(pen, panel.X + 24, panel.Y, panel.X + 90, panel.Y);
            }
        }

        // ---- Pages in the flow layouts

        float FlowTop(FlowSpec f) { return f.Area.Y + (f.Header ? 58 : 4); }

        Font FlowTitleFont(FlowSpec f) { return !f.Narrow && f.Area.Height >= 236 ? fontTitle : fontHeading; }

        float FlowTitleHeight(FlowSpec f) { return FlowTitleFont(f) == fontTitle ? 40 : 32; }

        /// A button's spot: in the bottom bar, or at the bottom right of the content
        RectangleF FlowButton(FlowSpec f, float width, float fromRight)
        {
            float y = f.Bar ? CardH - 53 : f.Area.Bottom - 42;
            float right = f.Bar ? CardW - Gutter : f.Area.Right;
            return new RectangleF(right - fromRight - width, y, width, 42);
        }

        /// A small note: in the bar, beside the buttons, or above them when narrow
        void FlowNote(Graphics g, FlowSpec f, string text, double alpha, bool besideButtons)
        {
            var color = Fade(Theme.Text2, alpha * 0.75);
            if (f.Bar) { DrawText(g, text, fontSmall, color, Gutter, CardH - 40); }
            else if (!besideButtons) { DrawText(g, text, fontSmall, color, f.Area.X, f.Area.Bottom - 18); }
            else if (!f.Narrow) { DrawText(g, text, fontSmall, color, f.Area.X, f.Area.Bottom - 28); }
            else { DrawText(g, text, fontSmall, color, f.Area.X, f.Area.Bottom - 66); }
        }

        float FlowParagraph(Graphics g, FlowSpec f, string text, double alpha, float y)
        {
            DrawText(g, text, fontBody, Fade(Theme.Text2, alpha), f.Area.X + 1, y, f.Area.Width);
            return g.MeasureString(text, fontBody, (int)f.Area.Width).Height;
        }

        void FlowWelcome(Graphics g, FlowSpec f)
        {
            double t = Now - screenStart;
            double b = EaseOut((t - 0.1) / 0.6);
            double c = EaseOut((t - 0.3) / 0.5);
            float slide = (float)((1 - b) * 12);
            float y = FlowTop(f);
            var title = f.Area.Width >= 380 ? fontHero : fontTitle;
            DrawText(g, f.WelcomeTitle, title, Fade(Theme.Text1, b), f.Area.X - 1 + slide, y);
            y += title == fontHero ? 50 : 40;
            y += FlowParagraph(g, f, "Twitch chat, made yours. It only takes a few seconds, and you can start chatting right after.", b, y) + 14;
            DrawCheckbox(g, c, f.Area.X + 2, y);
            bool wide = !f.Narrow || f.Bar;
            DrawButton(g, FlowButton(f, wide ? 180 : f.Area.Width, 0), wide ? "Install" : "Install Solrino", c, delegate { BeginInstall(); });
            if (f.Credit) { FlowNote(g, f, "Made by scary · a one-person dev team", c, true); }
        }

        void FlowImport(Graphics g, FlowSpec f)
        {
            double t = Now - screenStart;
            double text = EaseOut((t - 0.1) / 0.45);
            double controls = EaseOut((t - 0.25) / 0.45);
            float y = FlowTop(f);
            DrawText(g, "Bring your settings over?", FlowTitleFont(f), Fade(Theme.Text1, text), f.Area.X - 1, y);
            y += FlowTitleHeight(f);
            y += FlowParagraph(g, f, "Import your settings and chat tabs from another fork such as Moltorino or Bluzyrino.", text, y) + 10;
            for (int i = 0; i < forks.Count; i++)
            {
                DrawForkRow(g, new RectangleF(f.Area.X, y + i * 30, f.Area.Width, 26), i, controls);
            }
            y += Math.Max(1, forks.Count) * 30 + 2;
            DrawLink(g, "Choose another folder", y, controls, delegate { PickImportFolder(); }, f.Area.X);

            bool wide = !f.Narrow || f.Bar;
            float w = wide ? 146 : (f.Area.Width - 8) / 2;
            DrawButton(g, FlowButton(f, w, 0), "Import", controls, delegate { AnswerImport(forks[chosenFork].Folder); });
            DrawSecondaryButton(g, FlowButton(f, w, w + 8), "Start fresh", controls, delegate { AnswerImport(""); });
            if (wide) { FlowNote(g, f, "Your logins come too.", controls, true); }
        }

        void FlowInstalling(Graphics g, FlowSpec f)
        {
            string percent = (int)Math.Round(shownProgress * 100) + "%";
            float y = FlowTop(f) + 6;
            DrawText(g, "Installing", FlowTitleFont(f), Theme.Text1, f.Area.X - 1, y + 8);
            using (var brush = new SolidBrush(Theme.AccentBright))
            using (var format = new StringFormat())
            {
                format.Alignment = StringAlignment.Far;
                g.DrawString(percent, fontPercent, brush, new RectangleF(f.Area.X, y - 10, f.Area.Width, 60), format);
            }
            y += 62;
            DrawProgress(g, new RectangleF(f.Area.X, y, f.Area.Width, 12), shownProgress);
            y += 26;
            using (var brush = new SolidBrush(Theme.Text2))
            using (var format = new StringFormat())
            {
                format.Trimming = StringTrimming.EllipsisPath;
                format.FormatFlags = StringFormatFlags.NoWrap;
                g.DrawString(CurrentFileText(), fontSmall, brush, new RectangleF(f.Area.X, y, f.Area.Width, 18), format);
            }
            FlowNote(g, f, "This only takes a moment", 1, false);
        }

        void FlowDone(Graphics g, FlowSpec f)
        {
            double t = EaseOut((Now - screenStart) / 0.6);
            string next = options.Launch ? "Opening Solrino..." : "Solrino is installed";
            float y = FlowTop(f) + 6;
            DrawCheckMark(g, new PointF(f.Area.X + 22, y + 22), 22, t);
            DrawText(g, "You're all set", FlowTitleFont(f), Fade(Theme.Text1, t), f.Area.X + 56, y + (FlowTitleFont(f) == fontTitle ? 2 : 7));
            y += 64;
            DrawText(g, next, fontBody, Fade(Theme.Text2, t), f.Area.X + 1, y);
            DrawProgress(g, new RectangleF(f.Area.X, y + 42, f.Area.Width, 8), 1);
            if (f.Bar) { FlowNote(g, f, "Thanks for installing", t, false); }
        }

        void FlowProblem(Graphics g, FlowSpec f)
        {
            double t = EaseOut((Now - screenStart) / 0.35);
            float y = FlowTop(f);
            DrawText(g, "That didn't finish", FlowTitleFont(f), Fade(Theme.Text1, t), f.Area.X - 1, y);
            y += FlowTitleHeight(f) + 2;
            // A long error stops above the button instead of running into it
            float bottom = (f.Bar ? CardH - 64 : f.Area.Bottom - 42) - 12;
            using (var brush = new SolidBrush(Fade(Theme.Text2, t)))
            using (var format = new StringFormat())
            {
                format.Trimming = StringTrimming.EllipsisWord;
                g.DrawString(problem, fontBody, brush, new RectangleF(f.Area.X + 1, y, f.Area.Width, Math.Max(18, bottom - y)), format);
            }
            bool wide = !f.Narrow || f.Bar;
            DrawButton(g, FlowButton(f, wide ? 180 : f.Area.Width, 0), "Try again", t, delegate { BeginInstall(); });
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
                // Let Windows move it, exactly like dragging a title bar
                mouseDown = false;
                Native.ReleaseCapture();
                Native.SendMessage(Handle, 0x00A1 /* WM_NCLBUTTONDOWN */, (IntPtr)2 /* HTCAPTION */, IntPtr.Zero);
            }
        }

        bool moving;

        protected override void WndProc(ref Message m)
        {
            // While it's being moved the animation waits, so the move gets the
            // whole frame and follows the mouse without stutter
            if (m.Msg == 0x0231 /* WM_ENTERSIZEMOVE */) { moving = true; }
            else if (m.Msg == 0x0232 /* WM_EXITSIZEMOVE */) { moving = false; }
            base.WndProc(ref m);
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

                var blend = new Native.BLENDFUNCTION
                {
                    BlendOp = 0,
                    BlendFlags = 0,
                    SourceConstantAlpha = (byte)(255 * opacity),
                    AlphaFormat = 1
                };
                Native.UpdateLayeredWindow(Handle, screenDc, IntPtr.Zero, ref size, memDc, ref source, 0, ref blend, 2);
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
        public static extern bool UpdateLayeredWindow(IntPtr hwnd, IntPtr hdcDst, IntPtr pptDst, ref SIZE psize, IntPtr hdcSrc, ref POINT pptSrc, int crKey, ref BLENDFUNCTION pblend, int dwFlags);

        [DllImport("user32.dll")]
        public static extern bool ReleaseCapture();

        [DllImport("user32.dll")]
        public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);

        [DllImport("user32.dll")]
        public static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam);
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
