using System;
using UnityEngine;
using UnityEngine.Rendering;

namespace ArmadaSecondScreen
{
    // Owns an off-screen camera and ships what it renders to the second screen.
    public sealed class SurfaceStreamer : IDisposable
    {
        readonly Link link;
        public readonly Camera Cam;
        readonly GameObject go;
        RenderTexture rt;
        float next; int inFlight;
        public float Fps = 30f;
        public int Width { get; private set; }
        public int Height { get; private set; }

        public SurfaceStreamer(Link link, int w, int h)
        {
            this.link = link;
            go = new GameObject("ArmadaSecondScreenCam");
            go.hideFlags = HideFlags.HideAndDontSave;
            UnityEngine.Object.DontDestroyOnLoad(go);
            Cam = go.AddComponent<Camera>();
            Cam.enabled = false;
            Cam.orthographic = true;
            Cam.clearFlags = CameraClearFlags.SolidColor;
            Cam.backgroundColor = new Color(0.016f, 0.016f, 0.03f, 1f);
            Cam.allowHDR = false; Cam.allowMSAA = false; Cam.useOcclusionCulling = false;
            Resize(w, h);
        }

        public void Resize(int w, int h)
        {
            if (w == Width && h == Height && rt != null) return;
            Width = w; Height = h;
            var old = rt;
            rt = new RenderTexture(w, h, 16, RenderTextureFormat.ARGB32) { name = "ArmadaSecondScreenRT" };
            rt.Create();
            Cam.targetTexture = rt;
            Cam.aspect = (float)w / h;
            if (old != null) { old.Release(); UnityEngine.Object.Destroy(old); }
            full = true;
        }

        bool full = true, haveDamage, wasConnected;
        RectInt damage;

        public void Invalidate() { full = true; }

        // Pixel rect, origin bottom-left.
        public void Invalidate(RectInt r)
        {
            if (!haveDamage) { damage = r; haveDamage = true; return; }
            int x0 = Mathf.Min(damage.xMin, r.xMin), y0 = Mathf.Min(damage.yMin, r.yMin);
            damage = new RectInt(x0, y0, Mathf.Max(damage.xMax, r.xMax) - x0, Mathf.Max(damage.yMax, r.yMax) - y0);
        }

        // Render and read back only when something changed and a viewer is attached.
        public void Tick(bool haveContent)
        {
            if (!link.Connected) { wasConnected = false; return; }
            if (!wasConnected || link.NeedFull) { wasConnected = true; link.NeedFull = false; full = true; }
            if (!full && !haveDamage) return;
            if (Time.unscaledTime < next || inFlight >= 2) return;
            next = Time.unscaledTime + 1f / Fps;
            if (haveContent) Cam.Render();
            else
            {
                var prev = RenderTexture.active;
                RenderTexture.active = rt; GL.Clear(true, true, Cam.backgroundColor); RenderTexture.active = prev;
            }
            int sw = Width, sh = Height, x = 0, y = 0, w = sw, h = sh;
            if (!full)
            {
                x = Mathf.Clamp(damage.xMin, 0, sw - 1); y = Mathf.Clamp(damage.yMin, 0, sh - 1);
                w = Mathf.Clamp(damage.xMax, x + 1, sw) - x; h = Mathf.Clamp(damage.yMax, y + 1, sh) - y;
            }
            full = false; haveDamage = false;
            inFlight++;
            AsyncGPUReadback.Request(rt, 0, x, w, y, h, 0, 1, TextureFormat.RGBA32, req =>
            {
                inFlight--;
                if (req.hasError || sw != Width || sh != Height) { full = true; return; }
                link.OfferFrame(req.GetData<byte>(), sw, sh, x, y, w, h);
            });
        }

        public void Dispose()
        {
            if (go != null) UnityEngine.Object.Destroy(go);
            if (rt != null) { rt.Release(); UnityEngine.Object.Destroy(rt); }
        }
    }
}
