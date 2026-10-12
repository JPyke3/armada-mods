using System;
using System.IO;
using System.Linq;
using System.Reflection;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace ArmadaSecondScreen
{
    // Entry point for Unity Mono games. The launch-time patch makes
    // UnityEngine.Application's static constructor call Boot.Start(); nothing
    // else of the game is touched and no native hook library is involved.
    public static class Boot
    {
        static bool started;
        static GameObject host;

        public static void Start()
        {
            if (started) return;
            started = true;
            // Still inside Application's static constructor here: wait for the first scene.
            SceneManager.sceneLoaded += OnScene;
        }

        static void OnScene(Scene scene, LoadSceneMode mode)
        {
            if (host != null) return;
            host = new GameObject("ArmadaSecondScreenLoader");
            host.hideFlags = HideFlags.HideAndDontSave;
            UnityEngine.Object.DontDestroyOnLoad(host);
            host.AddComponent<BootBehaviour>();
        }
    }

    // Loads the newest provider DLL from <game>/ArmadaSecondScreen and reloads
    // it when a newer one appears (development only looks at the extra directory).
    public class BootBehaviour : MonoBehaviour
    {
        string dir, loadedPath, logPath;
        DateTime loadedStamp;
        GameObject payload;
        float nextCheck;

        void Log(string s)
        {
            try { File.AppendAllText(logPath, DateTime.Now.ToString("HH:mm:ss.fff ") + s + "\n"); } catch (Exception) { }
        }

        void Awake()
        {
            dir = Path.Combine(Application.dataPath, "..", "ArmadaSecondScreen");
            Directory.CreateDirectory(dir);
            logPath = Path.Combine(dir, "boot.log");
            Log("boot, unity " + Application.unityVersion);
        }

        void Update()
        {
            if (Time.unscaledTime < nextCheck) return;
            nextCheck = Time.unscaledTime + 1f;
            try
            {
                var files = new DirectoryInfo(dir).GetFiles("*.dll").AsEnumerable();
                var dev = Environment.GetEnvironmentVariable("ARMADA_MOD_DEV_DIR");
                if (!string.IsNullOrEmpty(dev) && Directory.Exists(dev)) files = files.Concat(new DirectoryInfo(dev).GetFiles("*.dll"));
                var newest = files.OrderByDescending(f => f.LastWriteTimeUtc).FirstOrDefault();
                if (newest == null) return;
                if (newest.FullName == loadedPath && newest.LastWriteTimeUtc == loadedStamp) return;
                loadedPath = newest.FullName;
                loadedStamp = newest.LastWriteTimeUtc;
                var asm = Assembly.Load(File.ReadAllBytes(newest.FullName));
                var entry = asm.GetTypes().First(t => t.Name == "Entry" && typeof(MonoBehaviour).IsAssignableFrom(t));
                if (payload != null) DestroyImmediate(payload);
                payload = new GameObject("ArmadaSecondScreen");
                payload.hideFlags = HideFlags.HideAndDontSave;
                DontDestroyOnLoad(payload);
                payload.AddComponent(entry);
                Log("loaded " + newest.Name);
            }
            catch (Exception e)
            {
                Log("payload load failed: " + e);
            }
        }
    }
}
