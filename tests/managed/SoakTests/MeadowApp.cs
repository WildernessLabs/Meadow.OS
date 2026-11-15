using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Reflection;
using System.Threading.Tasks;
using Meadow;
using Meadow.Devices;
using Meadow.Hardware;

public class MeadowApp : App<F7CoreComputeV2>
{
    const string Version = "1";

    //
    //  The CLI drops output printed before "app run" is listening (Meadow_Issues#902).
    //
    const int StartupDelayMs = 40000;
    const int WifiWaitSeconds = 90;
    const int SettleSeconds = 10;

    //
    //  Off: on OS 2.999.53.37 a mallinfo call during network traffic hangs the runtime within a
    //  window (HTTP alone, 3 windows of 60 s: hung in window 2 with it, passed without it).
    //
    static readonly bool MetricsMallinfo = false;

    const int OverrunSeconds = 120;

    static readonly Scenario[] Registry =
    {
        new("http", Status.Enabled, null, Scenarios.Http),
        new("gc", Status.Enabled, null, Scenarios.Gc),
        new("block", Status.Enabled, null, Scenarios.Block),
        new("sockets", Status.Enabled, null, Scenarios.Sockets),
        new("flash", Status.Enabled, null, Scenarios.Flash),
        new("i2c", Status.Enabled, null, Scenarios.I2c),
        new("gpio", Status.NeedsJumper, null, Scenarios.Gpio),
        new("ap", Status.Disabled, "S8 needs a switchable access point and a host-side driver", null),
        new("power", Status.Disabled, "S9 needs a USB power switch and a host-side driver", null),
    };

    int cases;
    int failures;

    //  A deploy raises the counters (118 to 120 for one meadow app run), so they are reported as the change since the start of the run.
    static (int Resets, int PowerCycles)? resetBaseline;

    public override async Task Run()
    {
        await Task.Delay(StartupDelayMs);
        Console.WriteLine($"TESTRUN BEGIN soak {Version}");
        SoakSettings.Load(Array.ConvertAll(Registry, s => s.Id));
        Console.WriteLine("TESTRUN INFO " + Metrics("start", 0));
        Console.WriteLine($"TESTRUN INFO settings windows={SoakSettings.Windows} window_s={SoakSettings.WindowSeconds} scenarios={string.Join(",", SoakSettings.Scenarios)} server={SoakSettings.Server} https={SoakSettings.HttpsUrl}");

        foreach (var id in SoakSettings.Scenarios)
        {
            if (Array.Find(Registry, s => s.Id == id) == null)
            {
                Console.WriteLine($"TESTRUN INFO unknown scenario {id}");
            }
        }
        var selected = new List<Scenario>();
        foreach (var scenario in Registry)
        {
            if (Array.IndexOf(SoakSettings.Scenarios, scenario.Id) < 0)
            {
                continue;
            }
            if (scenario.Status == Status.Disabled)
            {
                Console.WriteLine($"TESTRUN INFO disabled {scenario.Id}: {scenario.Reason}");
            }
            else
            {
                selected.Add(scenario);
            }
        }

        await Case("soak.wifi.connected", WaitForWiFi);

        foreach (var scenario in selected)
        {
            for (int number = 1; number <= SoakSettings.Windows; number++)
            {
                await RunScenario(scenario, new Window(number, TimeSpan.FromSeconds(SoakSettings.WindowSeconds)));
                Console.WriteLine("TESTRUN INFO " + Metrics(scenario.Id, number));
            }

            //
            //  The ESP32 needs time to free the sockets this scenario closed.
            //
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            await Task.Delay(SettleSeconds * 1000);
        }

        Console.WriteLine($"TESTRUN END failures={failures} cases={cases}");

        while (true)
        {
            await Task.Delay(60000);
        }
    }

    async Task RunScenario(Scenario scenario, Window window)
    {
        var body = Task.Run(() => scenario.Body(window));
        await Case($"soak.{scenario.Id}.w{window.Number:00}", async () =>
        {
            var overrun = Task.Delay(window.Length + TimeSpan.FromSeconds(OverrunSeconds));
            if (await Task.WhenAny(body, overrun) != body)
            {
                throw new TimeoutException($"still running {OverrunSeconds} s after the window");
            }
            return await body;
        });
    }

    async Task Case(string name, Func<Task<string>> body)
    {
        var watch = Stopwatch.StartNew();
        string status, detail;
        try
        {
            detail = await body();
            status = "PASS";
        }
        catch (SkipException ex)
        {
            status = "SKIP";
            detail = ex.Message;
        }
        catch (Exception ex)
        {
            status = "FAIL";
            detail = $"{ex.GetType().Name}: {ex.Message}";
        }
        lock (this)
        {
            cases++;
            if (status == "FAIL")
            {
                failures++;
            }
            Console.WriteLine($"TESTRUN CASE {name} {status} {watch.ElapsedMilliseconds} {detail.Replace('\n', ' ').Replace('\r', ' ')}");
        }
    }

    async Task<string> WaitForWiFi()
    {
        var wifi = Resolver.Device.NetworkAdapters.Primary<IWiFiNetworkAdapter>();
        if (wifi == null)
        {
            throw new InvalidOperationException("no WiFi adapter");
        }
        for (int i = 0; i < WifiWaitSeconds; i++)
        {
            var address = WirelessAddress();
            if (address != null)
            {
                Scenarios.WifiUp = true;
                return $"ip={address} adapter_state={(wifi.IsConnected ? "connected" : "not-connected")}";
            }
            await Task.Delay(1000);
        }
        throw new TimeoutException($"WiFi did not connect in {WifiWaitSeconds} s (adapter connected={wifi.IsConnected})");
    }

    static string WirelessAddress()
    {
        foreach (var nic in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (nic.NetworkInterfaceType != NetworkInterfaceType.Wireless80211)
            {
                continue;
            }
            foreach (var unicast in nic.GetIPProperties().UnicastAddresses)
            {
                var ip = unicast.Address;
                if ((ip.AddressFamily == AddressFamily.InterNetwork) &&
                    (!ip.Equals(IPAddress.Any)) && (!ip.Equals(IPAddress.Broadcast)))
                {
                    return ip.ToString();
                }
            }
        }
        return null;
    }

    static string Metrics(string scenario, int window)
    {
        var line = $"metric s={scenario} w={window:00} gc_bytes={GC.GetTotalMemory(true)}";
        if (MetricsMallinfo)
        {
            line += " " + Read(() =>
            {
                var m = Resolver.Device.PlatformOS.GetMemoryAllocationInfo();
                return $"arena={m.Arena} alloc={m.TotalAllocated} free={m.TotalFree} largest_free={m.LargestFreeBlock} free_blocks={m.FreeBlocks}";
            }, "mem=n/a");
        }
        line += " " + Read(() => "cpu_pct=" + string.Join("/", Resolver.Device.PlatformOS.GetProcessorUtilization()), "cpu_pct=n/a");
        line += " " + Read(() =>
        {
            var r = Resolver.Services.Get<IReliabilityService>();
            resetBaseline ??= (r.SystemResetCount, r.SystemPowerCycleCount);
            return $"reset_reason={r.LastResetReason} resets=+{r.SystemResetCount - resetBaseline.Value.Resets} power_cycles=+{r.SystemPowerCycleCount - resetBaseline.Value.PowerCycles}";
        }, "resets=n/a");
        return line;
    }

    static string Read(Func<string> source, string fallback)
    {
        try
        {
            return source();
        }
        catch (Exception)
        {
            return fallback;
        }
    }
}

enum Status { Enabled, NeedsJumper, Disabled }

sealed record Scenario(string Id, Status Status, string Reason, Func<Window, Task<string>> Body);

sealed class SkipException : Exception
{
    public SkipException(string reason) : base(reason) { }
}

sealed class Window
{
    readonly Stopwatch watch = Stopwatch.StartNew();

    public Window(int number, TimeSpan length)
    {
        Number = number;
        Length = length;
    }

    public int Number { get; }
    public TimeSpan Length { get; }
    public bool Over => watch.Elapsed >= Length;
    public TimeSpan Remaining => Over ? TimeSpan.Zero : Length - watch.Elapsed;

    public Task Sleep(int ms) => Task.Delay((int) Math.Min(ms, Remaining.TotalMilliseconds));
}

//
//  The app runs on the board, out of reach of the runner's environment, so the build bakes the
//  MEADOW_SOAK_* variables into assembly metadata (SoakTests.csproj).
//
static class SoakSettings
{
    public static int Windows = 2;
    public static int WindowSeconds = 300;
    public static string[] Scenarios;
    public static string Server = "192.168.1.65";
    public static string HttpsUrl = "https://example.com/";

    public static void Load(string[] all)
    {
        Windows = Int("MEADOW_SOAK_WINDOWS", Windows);
        WindowSeconds = Int("MEADOW_SOAK_WINDOW_S", WindowSeconds);
        Server = Text("MEADOW_SOAK_SERVER", Server);
        HttpsUrl = Text("MEADOW_SOAK_HTTPS_URL", HttpsUrl);
        var list = Text("MEADOW_SOAK_SCENARIOS", null);
        Scenarios = (list == null) ? all : list.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
    }

    static string Text(string key, string fallback)
    {
        foreach (var attribute in typeof(MeadowApp).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>())
        {
            if ((attribute.Key == key) && !string.IsNullOrEmpty(attribute.Value))
            {
                return attribute.Value;
            }
        }
        return fallback;
    }

    static int Int(string key, int fallback) => int.TryParse(Text(key, null), out var value) && (value > 0) ? value : fallback;
}
