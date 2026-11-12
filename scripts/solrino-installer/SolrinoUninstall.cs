// Solrino's uninstaller. The setup puts it in the install folder as
// "Uninstall Solrino.exe" and points Installed apps at it.
//
// It removes the app files, shortcuts and the Installed apps entry. Your
// settings in %APPDATA%\Solrino are kept, so a reinstall picks them up.
//
// A running program can't delete itself, so after the questions it copies
// itself to the temp folder and that copy finishes the job once this one exits.

using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

namespace SolrinoUninstall
{
    static class Program
    {
        const string Title = "Solrino";
        const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\Solrino";

        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            if (args.Length == 3 && args[0] == "--finish")
            {
                return Finish(args[1], args[2]);
            }
            return Start();
        }

        static int Start()
        {
            string self = Process.GetCurrentProcess().MainModule.FileName;
            string folder = Path.GetDirectoryName(self);

            if (!File.Exists(Path.Combine(folder, "Solrino.exe")))
            {
                MessageBox.Show("Solrino was not found in this folder.", Title);
                return 1;
            }

            var answer = MessageBox.Show(
                "Remove Solrino from this computer?\n\nYour settings stay in %APPDATA%\\Solrino.",
                Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (answer != DialogResult.Yes) { return 0; }

            if (IsRunningFrom(folder))
            {
                MessageBox.Show("Quit Solrino from its tray icon first, then try again.", Title);
                return 1;
            }

            foreach (var place in new[] {
                Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                Environment.GetFolderPath(Environment.SpecialFolder.Programs) })
            {
                TryDelete(Path.Combine(place, "Solrino.lnk"));
            }
            try { Registry.CurrentUser.DeleteSubKeyTree(UninstallKey, false); }
            catch (Exception) { }

            // Hand the folder over to a copy outside it
            string helper = Path.Combine(Path.GetTempPath(), "Solrino-Uninstall-" + Guid.NewGuid().ToString("N") + ".exe");
            try
            {
                File.Copy(self, helper, true);
                var start = new ProcessStartInfo(helper);
                start.Arguments = "--finish \"" + folder + "\" " + Process.GetCurrentProcess().Id;
                start.UseShellExecute = false;
                Process.Start(start);
            }
            catch (Exception)
            {
                // Couldn't hand off: remove everything but this file
                DeleteFolder(folder, self);
            }
            return 0;
        }

        static int Finish(string folder, string parentId)
        {
            int id;
            if (int.TryParse(parentId, out id))
            {
                try { Process.GetProcessById(id).WaitForExit(15000); }
                catch (ArgumentException) { }
            }

            // Only ever a folder that holds Solrino, never anything else
            if (Directory.Exists(folder) && File.Exists(Path.Combine(folder, "Solrino.exe")))
            {
                for (int attempt = 0; attempt < 10 && Directory.Exists(folder); attempt++)
                {
                    DeleteFolder(folder, null);
                    if (Directory.Exists(folder)) { Thread.Sleep(500); }
                }
            }
            MessageBox.Show("Solrino was removed.", Title);
            return 0;
        }

        static bool IsRunningFrom(string folder)
        {
            string prefix = folder.TrimEnd('\\') + "\\";
            foreach (var process in Process.GetProcessesByName("Solrino"))
            {
                try
                {
                    if (process.MainModule.FileName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    {
                        return true;
                    }
                }
                catch (Exception) { }
            }
            return false;
        }

        static void DeleteFolder(string folder, string keep)
        {
            if (keep == null)
            {
                try { Directory.Delete(folder, true); } catch (Exception) { }
                return;
            }
            foreach (var file in Directory.GetFiles(folder, "*", SearchOption.AllDirectories))
            {
                if (!string.Equals(file, keep, StringComparison.OrdinalIgnoreCase)) { TryDelete(file); }
            }
            foreach (var sub in Directory.GetDirectories(folder))
            {
                try { Directory.Delete(sub, true); } catch (Exception) { }
            }
        }

        static void TryDelete(string path)
        {
            try { if (File.Exists(path)) { File.Delete(path); } }
            catch (Exception) { }
        }
    }
}
