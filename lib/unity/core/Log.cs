using System;

namespace ArmadaSecondScreen
{
    public static class Log
    {
        static string path;
        public static void Init(string p) { path = p; }
        public static void Info(string s)
        {
            try { System.IO.File.AppendAllText(path, DateTime.Now.ToString("HH:mm:ss.fff ") + s + "\n"); } catch (Exception) { }
        }
    }
}
