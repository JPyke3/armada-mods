using System;
using System.Collections.Concurrent;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Unity.Collections;

namespace ArmadaSecondScreen
{
    // Wire format, little endian: "A2SS" u16 type u16 flags u32 len u32 seq, then len bytes.
    public static class Msg
    {
        public const ushort Hello = 1, Frame = 2, State = 3;
        public const ushort Input = 16, Config = 17, Cmd = 18;
    }

    public struct Incoming { public ushort Type; public string Text; }

    public sealed class Link : IDisposable
    {
        readonly string host; readonly int port; readonly string hello;
        readonly Thread thread;
        readonly object gate = new object();
        readonly ConcurrentQueue<KeyValuePair2> outbox = new ConcurrentQueue<KeyValuePair2>();
        public readonly ConcurrentQueue<Incoming> Inbox = new ConcurrentQueue<Incoming>();
        volatile bool stop, connected;
        volatile TcpClient active;
        uint seq;
        public bool Connected => connected;
        public long FramesSent;

        struct KeyValuePair2 { public ushort Type; public byte[] Data; }

        public Link(string host, int port, string helloJson)
        {
            this.host = host; this.port = port; hello = helloJson;
            thread = new Thread(Run) { IsBackground = true, Name = "a2ss-link" };
            thread.Start();
        }

        public void Dispose()
        {
            stop = true;
            try { active?.Close(); } catch (Exception) { }
            lock (gate) Monitor.PulseAll(gate);
        }

        public void SendText(ushort type, string text)
        {
            if (!connected) return;
            outbox.Enqueue(new KeyValuePair2 { Type = type, Data = Encoding.UTF8.GetBytes(text) });
            lock (gate) Monitor.PulseAll(gate);
        }

        // Frames are queued in order because partial updates build on earlier ones.
        // A backlog means the viewer is slow: drop it and ask for a full resend.
        public volatile bool NeedFull;

        public void OfferFrame(NativeArray<byte> data, int surfW, int surfH, int x, int y, int w, int h)
        {
            if (!connected) return;
            if (outbox.Count > 4)
            {
                // only frames are dropped; state messages stay queued in order
                int n = outbox.Count; bool dropped = false;
                while (n-- > 0 && outbox.TryDequeue(out var m))
                    if (m.Type == Msg.Frame) dropped = true; else outbox.Enqueue(m);
                if (dropped) { NeedFull = true; return; }
            }
            var body = new byte[16 + data.Length];
            BitConverter.GetBytes((ushort)surfW).CopyTo(body, 0);
            BitConverter.GetBytes((ushort)surfH).CopyTo(body, 2);
            body[4] = 1; // RGBA8, rows bottom-up, rect origin bottom-left
            BitConverter.GetBytes((ushort)x).CopyTo(body, 8);
            BitConverter.GetBytes((ushort)y).CopyTo(body, 10);
            BitConverter.GetBytes((ushort)w).CopyTo(body, 12);
            BitConverter.GetBytes((ushort)h).CopyTo(body, 14);
            NativeArray<byte>.Copy(data, 0, body, 16, data.Length);
            outbox.Enqueue(new KeyValuePair2 { Type = Msg.Frame, Data = body });
            FramesSent++;
            lock (gate) Monitor.PulseAll(gate);
        }

        void Run()
        {
            while (!stop)
            {
                TcpClient c = null;
                try
                {
                    c = new TcpClient { NoDelay = true, SendTimeout = 2000 };
                    active = c;
                    c.Connect(host, port);
                    var s = c.GetStream();
                    while (outbox.TryDequeue(out _)) { }
                    Write(s, Msg.Hello, Encoding.UTF8.GetBytes(hello));
                    connected = true;
                    var reader = new Thread(() => Read(s)) { IsBackground = true, Name = "a2ss-read" };
                    reader.Start();
                    while (!stop && connected)
                    {
                        lock (gate) { if (outbox.IsEmpty) Monitor.Wait(gate, 500); }
                        while (outbox.TryDequeue(out var m)) Write(s, m.Type, m.Data);
                    }
                }
                catch (Exception) { }
                connected = false;
                try { c?.Close(); } catch (Exception) { }
                if (!stop) Thread.Sleep(1000);
            }
        }

        void Write(Stream s, ushort type, byte[] data)
        {
            var hd = new byte[16];
            hd[0] = (byte)'A'; hd[1] = (byte)'2'; hd[2] = (byte)'S'; hd[3] = (byte)'S';
            BitConverter.GetBytes(type).CopyTo(hd, 4);
            BitConverter.GetBytes((uint)data.Length).CopyTo(hd, 8);
            BitConverter.GetBytes(seq++).CopyTo(hd, 12);
            s.Write(hd, 0, 16);
            s.Write(data, 0, data.Length);
        }

        void Read(Stream s)
        {
            try
            {
                var hd = new byte[16];
                while (!stop && connected)
                {
                    Fill(s, hd, 16);
                    if (hd[0] != 'A' || hd[1] != '2' || hd[2] != 'S' || hd[3] != 'S') break;
                    ushort type = BitConverter.ToUInt16(hd, 4);
                    int len = (int)BitConverter.ToUInt32(hd, 8);
                    if (len < 0 || len > (1 << 20)) break;
                    var body = new byte[len];
                    Fill(s, body, len);
                    Inbox.Enqueue(new Incoming { Type = type, Text = Encoding.UTF8.GetString(body) });
                }
            }
            catch (Exception) { }
            connected = false;
            lock (gate) Monitor.PulseAll(gate);
        }

        static void Fill(Stream s, byte[] b, int n)
        {
            int o = 0;
            while (o < n)
            {
                int r = s.Read(b, o, n - o);
                if (r <= 0) throw new EndOfStreamException();
                o += r;
            }
        }
    }
}
