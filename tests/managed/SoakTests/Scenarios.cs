using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Meadow;
using Meadow.Devices;
using Meadow.Hardware;

static class Scenarios
{
    public static bool WifiUp;

    //
    //  The web server queues 5 connections, so at most 4 are held open to it.
    //
    const int WebPort = 80;
    const int IdlePort = 8081;
    const string BlackholeAddress = "10.255.255.1";

    const int HttpIntervalMs = 2000;
    const int HttpsIntervalMs = 30000;
    const int HttpTimeoutSeconds = 60;
    const int HttpMaxFailPercent = 1;

    //
    //  The OS heap fragments: within minutes the largest free block is 40 to 64 KB with 15 to 20 MB
    //  free, so an array over about 48 KB throws OutOfMemoryException.  Every size in this class
    //  stays below that.
    //
    const int GcWarmupWindows = 1;
    const long GcGrowthLimitBytes = 1024 * 1024;
    const int GcLoopPauseMs = 50;
    const int GcTreeDepth = 8;
    const int GcLargeArrays = 4;
    const int GcLargeBytes = 16 * 1024;

    const int BlockGcPeriodMs = 15000;
    const int BlockGcPauseLimitMs = 2000;
    const int BlockSpinBurstMs = 6000;
    const int BlockSpinRestMs = 4000;
    const int BlockReleaseSeconds = 20;
    const int BlockGarbageBytes = 8 * 1024;

    const int SocketBurstCount = 24;
    const int SocketConnectSeconds = 15;
    const int SocketBlackholeCancelSeconds = 30;
    const int SocketBlackholeLimitSeconds = 60;
    const int SocketRecoveryOpen = 4;
    const int SocketPauseMs = 5000;

    const string FlashDirectory = "soak";
    const int FlashWindowBytes = 256 * 1024;
    const int FlashFileMaxBytes = 32 * 1024;
    const int FlashChunkBytes = 4 * 1024;
    const int FlashPaceMs = 10000;

    // Project Lab v3 puts the BME688 and BMI270 on its peripheral bus, I2C bus 1.
    const int I2cIntervalMs = 100;
    static readonly (string Name, byte Address, byte Register, byte Id)[] I2cChips =
    {
        ("bme688", 0x76, 0xD0, 0x61),
        ("bmi270", 0x68, 0x00, 0x24),
    };

    // MikroBus 1 PWM (PB8) and INT (PC2): no Project Lab v3 part uses them.
    const string GpioPins = "PB8-PC2";
    const int GpioToggles = 500;
    const int GpioToggleMs = 5;
    const int GpioPauseMs = 5000;

    static void RequireWifi()
    {
        if (!WifiUp)
        {
            throw new InvalidOperationException("WiFi is not connected");
        }
    }

    static readonly HttpClient Client = new HttpClient { Timeout = TimeSpan.FromSeconds(HttpTimeoutSeconds) };

    static async Task Get(string url)
    {
        using (var response = await Client.GetAsync(url))
        {
            await response.Content.ReadAsStringAsync();
            if (!response.IsSuccessStatusCode)
            {
                throw new InvalidOperationException($"status {(int) response.StatusCode}");
            }
        }
    }

    static string Describe(Exception ex) => ex.GetType().Name + ((ex.InnerException == null) ? "" : " <- " + Describe(ex.InnerException) + ": " + ex.InnerException.Message);

    static IPEndPoint Endpoint(string address, int port) => new IPEndPoint(IPAddress.Parse(address), port);

    static async Task Connect(Socket socket, IPEndPoint endpoint, int seconds)
    {
        using (var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(seconds)))
        {
            await socket.ConnectAsync(endpoint, cancel.Token);
        }
    }

    static int httpsFailuresRunning;

    public static async Task<string> Http(Window w)
    {
        RequireWifi();
        int requests = 0, failures = 0, httpsRequests = 0, httpsFailures = 0;
        string lastError = "";
        var clock = Stopwatch.StartNew();
        long nextHttps = 0;
        while (!w.Over)
        {
            requests++;
            try
            {
                await Get($"http://{SoakSettings.Server}/");
            }
            catch (Exception ex)
            {
                failures++;
                lastError = Describe(ex);
            }
            if (clock.ElapsedMilliseconds >= nextHttps)
            {
                nextHttps = clock.ElapsedMilliseconds + HttpsIntervalMs;
                httpsRequests++;
                try
                {
                    await Get(SoakSettings.HttpsUrl);
                    httpsFailuresRunning = 0;
                }
                catch (Exception ex)
                {
                    httpsFailures++;
                    httpsFailuresRunning++;
                    lastError = "https " + Describe(ex);
                    if (httpsFailuresRunning >= 2)
                    {
                        throw new InvalidOperationException($"HTTPS failed twice running ({lastError}) req={requests} fail={failures}");
                    }
                }
            }
            await w.Sleep(HttpIntervalMs);
        }
        var detail = $"req={requests} fail={failures} https_req={httpsRequests} https_fail={httpsFailures}";
        if (failures * 100 > requests * HttpMaxFailPercent)
        {
            throw new InvalidOperationException($"{detail} last_error={lastError}");
        }
        return detail;
    }

    sealed class Node
    {
        public Node Left, Right;
        public byte[] Payload = new byte[64];
    }

    sealed class Finalizable
    {
        ~Finalizable() => Interlocked.Increment(ref finalized);
    }

    static long finalized;
    static long sink;
    static long gcBaseline = -1;
    static Node tree;
    static byte[][] large = new byte[GcLargeArrays][];
    static readonly WeakReference[] weak = new WeakReference[64];

    static Node Build(int depth) => (depth == 0) ? null : new Node { Left = Build(depth - 1), Right = Build(depth - 1) };

    static void Churn(int loops)
    {
        for (int i = 0; i < 500; i++)
        {
            var small = new byte[32 + (i % 96)];
            sink += small.Length + ("soak " + i + " " + loops).Length;
        }
        tree = Build(GcTreeDepth);
        large[loops % GcLargeArrays] = new byte[GcLargeBytes];
        for (int i = 0; i < 50; i++)
        {
            _ = new Finalizable();
        }
        weak[loops % weak.Length] = new WeakReference(new byte[1024]);
    }

    static void DropRetained()
    {
        tree = null;
        large = new byte[GcLargeArrays][];
    }

    public static async Task<string> Gc(Window w)
    {
        int loops = 0;
        try
        {
            while (!w.Over)
            {
                Churn(loops++);
                await w.Sleep(GcLoopPauseMs);
            }
        }
        catch (OutOfMemoryException ex)
        {
            throw new OutOfMemoryException($"after {loops} loops, gc_bytes={GC.GetTotalMemory(false)}: {ex.Message}");
        }
        finally
        {
            DropRetained();
        }

        GC.Collect();
        GC.WaitForPendingFinalizers();
        long bytes = GC.GetTotalMemory(true);
        int alive = 0;
        foreach (var reference in weak)
        {
            if ((reference != null) && reference.IsAlive)
            {
                alive++;
            }
        }
        if (w.Number == GcWarmupWindows)
        {
            gcBaseline = bytes;
        }
        var detail = $"loops={loops} gc_bytes={bytes} baseline={gcBaseline} finalized={Interlocked.Read(ref finalized)} weak_alive={alive}";
        if ((w.Number > GcWarmupWindows) && (gcBaseline >= 0) && (bytes > gcBaseline + GcGrowthLimitBytes))
        {
            throw new InvalidOperationException($"{detail}, more than {GcGrowthLimitBytes} over the baseline");
        }
        return detail;
    }

    public static async Task<string> Block(Window w)
    {
        RequireWifi();
        using (var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
        using (var connected = new ManualResetEventSlim())
        using (var stop = new ManualResetEventSlim())
        {
            Exception blockedError = null;
            int received = -1;
            long blockedMs = -1;
            var blocked = new Thread(() =>
            {
                try
                {
                    socket.Connect(Endpoint(SoakSettings.Server, IdlePort));
                    connected.Set();
                    var wait = Stopwatch.StartNew();
                    try
                    {
                        received = socket.Receive(new byte[256]);
                    }
                    finally
                    {
                        blockedMs = wait.ElapsedMilliseconds;
                    }
                }
                catch (Exception ex)
                {
                    blockedError = ex;
                }
                finally
                {
                    connected.Set();
                }
            }) { IsBackground = true };
            blocked.Start();
            if (!connected.Wait(TimeSpan.FromSeconds(SocketConnectSeconds * 2)) || (blockedError != null))
            {
                throw new InvalidOperationException($"blocked thread could not connect: {blockedError?.GetType().Name}");
            }

            var spinner = new Thread(() =>
            {
                long x = 0;
                while (!stop.IsSet)
                {
                    var burst = Stopwatch.StartNew();
                    while ((burst.ElapsedMilliseconds < BlockSpinBurstMs) && !stop.IsSet)
                    {
                        for (int i = 0; i < 10000; i++)
                        {
                            x += i * (long) i;
                        }
                    }
                    Thread.Sleep(BlockSpinRestMs);
                }
                sink += x;
            }) { IsBackground = true };
            spinner.Start();

            var churn = Task.Run(async () =>
            {
                for (int loops = 0; !w.Over; loops++)
                {
                    Churn(loops);
                    await w.Sleep(GcLoopPauseMs);
                }
            });
            var traffic = Http(w);

            long worst = 0;
            int collects = 0;
            try
            {
                while (!w.Over)
                {
                    for (int i = 0; i < 32; i++)
                    {
                        sink += new byte[BlockGarbageBytes].Length;
                    }
                    var pause = Stopwatch.StartNew();
                    GC.Collect();
                    worst = Math.Max(worst, pause.ElapsedMilliseconds);
                    collects++;
                    await w.Sleep(BlockGcPeriodMs);
                }
            }
            finally
            {
                stop.Set();
            }
            string http = null;
            Exception trafficError = null;
            try
            {
                await churn;
                http = await traffic;
            }
            catch (Exception ex)
            {
                trafficError = ex;
            }
            finally
            {
                DropRetained();
            }

            socket.Send(System.Text.Encoding.ASCII.GetBytes($"GET / HTTP/1.0\r\nHost: {SoakSettings.Server}\r\n\r\n"));
            if (!blocked.Join(TimeSpan.FromSeconds(BlockReleaseSeconds)))
            {
                throw new TimeoutException($"the blocked thread was not released {BlockReleaseSeconds} s after the response was requested");
            }
            if ((blockedError != null) || (received < 5))
            {
                throw new InvalidOperationException($"the blocked receive ended after {blockedMs} ms with {(blockedError == null ? "no error" : Describe(blockedError) + " " + (blockedError as SocketException)?.SocketErrorCode + ": " + blockedError.Message)} bytes={received}");
            }
            if (trafficError != null)
            {
                throw new InvalidOperationException($"load beside the blocked thread failed: {Describe(trafficError)}");
            }
            var detail = $"gc_collects={collects} gc_pause_max_ms={worst} {http}";
            if (worst > BlockGcPauseLimitMs)
            {
                throw new InvalidOperationException($"{detail}, limit {BlockGcPauseLimitMs}");
            }
            return detail;
        }
    }

    public static async Task<string> Sockets(Window w)
    {
        RequireWifi();
        int bursts = 0, opened = 0, failed = 0, unknown = 0;
        var errors = new SortedDictionary<string, int>();
        string blackhole = "not run";
        while ((bursts == 0) || !w.Over)
        {
            var sockets = new List<Socket>();
            var connects = new Task<string>[SocketBurstCount];
            for (int i = 0; i < SocketBurstCount; i++)
            {
                connects[i] = TryConnect(sockets);
            }
            foreach (var error in await Task.WhenAll(connects))
            {
                if (error == null)
                {
                    opened++;
                    continue;
                }
                failed++;
                errors[error] = errors.GetValueOrDefault(error) + 1;
                if (error.Contains("Unknown socket error"))
                {
                    unknown++;
                }
            }
            foreach (var socket in sockets)
            {
                socket.Dispose();
            }
            bursts++;
            await Task.Delay(2000);

            if (bursts == 1)
            {
                blackhole = await BlackholeConnect();
            }
            await Recover();
            await w.Sleep(SocketPauseMs);
        }
        var detail = $"bursts={bursts} opened={opened} failed={failed} unknown={unknown} errors={string.Join(",", errors)} blackhole={blackhole}";
        if (unknown > 0)
        {
            throw new InvalidOperationException($"connects over the socket limit gave \"Unknown socket error\": {detail}");
        }
        return detail;
    }

    static async Task<string> TryConnect(List<Socket> sockets)
    {
        try
        {
            var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            sockets.Add(socket);
            await Connect(socket, Endpoint(SoakSettings.Server, IdlePort), SocketConnectSeconds);
            return null;
        }
        catch (SocketException ex)
        {
            return $"{ex.SocketErrorCode}:{ex.Message}";
        }
        catch (Exception ex)
        {
            return ex.GetType().Name;
        }
    }

    static async Task Recover()
    {
        var held = new List<Socket>();
        try
        {
            for (int i = 0; i < SocketRecoveryOpen; i++)
            {
                var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                held.Add(socket);
                try
                {
                    await Connect(socket, Endpoint(SoakSettings.Server, WebPort), SocketConnectSeconds);
                }
                catch (Exception ex)
                {
                    throw new InvalidOperationException($"no recovery: connect {i} to the web server failed: {ex.GetType().Name}: {ex.Message}");
                }
            }
        }
        finally
        {
            foreach (var socket in held)
            {
                socket.Dispose();
            }
        }
        try
        {
            await Get($"http://{SoakSettings.Server}/");
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException($"no recovery: HTTP GET failed: {ex.GetType().Name}: {ex.Message}");
        }
    }

    static async Task<string> BlackholeConnect()
    {
        var watch = Stopwatch.StartNew();
        string outcome;
        using (var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
        {
            try
            {
                await Connect(socket, Endpoint(BlackholeAddress, 81), SocketBlackholeCancelSeconds);
                outcome = "connected";
            }
            catch (OperationCanceledException)
            {
                outcome = "cancelled";
            }
            catch (SocketException ex)
            {
                outcome = $"error {ex.SocketErrorCode}";
            }
        }
        long seconds = watch.ElapsedMilliseconds / 1000;
        if (outcome == "connected")
        {
            throw new InvalidOperationException($"connected to {BlackholeAddress}, which should never answer");
        }
        if (seconds > SocketBlackholeLimitSeconds)
        {
            throw new TimeoutException($"blackhole connect {outcome} after {seconds} s, limit {SocketBlackholeLimitSeconds}");
        }
        return $"{outcome}_after_{seconds}s";
    }

    public static async Task<string> Flash(Window w)
    {
        var directory = Path.Combine(MeadowOS.FileSystem.DataDirectory, FlashDirectory);
        Clean(directory);
        Directory.CreateDirectory(directory);
        var random = new Random(w.Number);
        var chunk = new byte[FlashChunkBytes];
        long written = 0;
        int cycles = 0;
        try
        {
            while (!w.Over)
            {
                int chunks = random.Next(1, FlashFileMaxBytes / FlashChunkBytes);
                long length = (chunks + 1L) * FlashChunkBytes;
                if (written + length > FlashWindowBytes)
                {
                    await w.Sleep(1000);
                    continue;
                }
                var path = Path.Combine(directory, $"f{cycles}.dat");
                uint expected = Crc32Start;
                for (int part = 0; part < chunks + 1; part++)
                {
                    random.NextBytes(chunk);
                    expected = Crc32Update(expected, chunk, chunk.Length);
                    using (var stream = new FileStream(path, (part == 0) ? FileMode.Create : FileMode.Append))
                    {
                        stream.Write(chunk, 0, chunk.Length);
                        stream.Flush(true);
                    }
                }
                var renamed = path + ".moved";
                File.Move(path, renamed);
                uint actual = Crc32Start;
                long read = 0;
                using (var stream = File.OpenRead(renamed))
                {
                    int count;
                    while ((count = stream.Read(chunk, 0, chunk.Length)) > 0)
                    {
                        actual = Crc32Update(actual, chunk, count);
                        read += count;
                    }
                }
                if ((read != length) || (actual != expected))
                {
                    throw new InvalidOperationException($"read-back mismatch in {Path.GetFileName(renamed)}: {read} bytes against {length}, crc {~actual:X8} against {~expected:X8}");
                }
                File.Delete(renamed);
                written += length;
                cycles++;
                await w.Sleep(FlashPaceMs);
            }
        }
        finally
        {
            Clean(directory);
        }
        if (Directory.Exists(directory))
        {
            throw new InvalidOperationException("the soak directory is still there after the clean-up");
        }
        return $"cycles={cycles} bytes_written={written}";
    }

    static void Clean(string directory)
    {
        if (Directory.Exists(directory))
        {
            Directory.Delete(directory, true);
        }
    }

    const uint Crc32Start = 0xFFFFFFFF;

    static uint Crc32Update(uint crc, byte[] data, int count)
    {
        for (int i = 0; i < count; i++)
        {
            crc ^= data[i];
            for (int bit = 0; bit < 8; bit++)
            {
                crc = ((crc & 1) != 0) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
            }
        }
        return crc;
    }

    static II2cBus i2cBus;

    public static async Task<string> I2c(Window w)
    {
        i2cBus ??= Resolver.Device.CreateI2cBus(1, I2cBusSpeed.Standard);
        int reads = 0, errors = 0;
        string first = null;
        var request = new byte[1];
        var reply = new byte[1];
        while (!w.Over)
        {
            foreach (var chip in I2cChips)
            {
                try
                {
                    request[0] = chip.Register;
                    i2cBus.Exchange(chip.Address, request, reply);
                    reads++;
                    if (reply[0] != chip.Id)
                    {
                        errors++;
                        first ??= $"{chip.Name} id 0x{reply[0]:X2}, expected 0x{chip.Id:X2}";
                    }
                }
                catch (Exception ex)
                {
                    errors++;
                    first ??= $"{chip.Name} {ex.GetType().Name}: {ex.Message}";
                }
            }
            await w.Sleep(I2cIntervalMs);
        }
        var detail = $"reads={reads} errors={errors}";
        if (errors > 0)
        {
            throw new InvalidOperationException($"{detail} first={first}");
        }
        return detail;
    }

    public static async Task<string> Gpio(Window w)
    {
        var device = (F7CoreComputeV2) Resolver.Device;
        using (var output = device.CreateDigitalOutputPort(device.Pins.PB8, false))
        using (var input = device.CreateDigitalInterruptPort(device.Pins.PC2, InterruptMode.EdgeBoth, ResistorMode.InternalPullDown))
        {
            for (int check = 0; check < 2; check++)
            {
                output.State = true;
                await Task.Delay(10);
                bool high = input.State;
                output.State = false;
                await Task.Delay(10);
                if (!high || input.State)
                {
                    throw new SkipException($"no jumper on {GpioPins}");
                }
            }

            int edges = 0;
            input.Changed += (sender, e) => Interlocked.Increment(ref edges);
            int toggles = 0, counted = 0, bad = 0;
            while (!w.Over)
            {
                Volatile.Write(ref edges, 0);
                for (int i = 0; i < GpioToggles; i++)
                {
                    output.State = (i % 2) == 0;
                    await Task.Delay(GpioToggleMs);
                }
                await Task.Delay(200);
                int seen = Volatile.Read(ref edges);
                toggles += GpioToggles;
                counted += seen;
                if (seen != GpioToggles)
                {
                    bad++;
                }
                await w.Sleep(GpioPauseMs);
            }
            var detail = $"toggles={toggles} edges={counted}";
            if (bad > 0)
            {
                throw new InvalidOperationException($"{detail}, {bad} bursts with the wrong edge count");
            }
            return detail;
        }
    }
}
