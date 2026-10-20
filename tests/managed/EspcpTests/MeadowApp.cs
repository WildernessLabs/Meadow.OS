//
//  espcp network robustness tests (issue #708): the bounded waits, SO_SNDTIMEO on new sockets and
//  the poll teardown changes, checked from the managed side over the real WiFi path.  Output
//  follows the test runner contract (tools/testrunner/README.md).
//
//  Assumes wifi.config.yaml was written to the board earlier, so the ESP32 holds the default access
//  point, and needs a web server at LocalServer.
//
//  What this can and cannot show: a request the ESP32 never answers is the only way to reach the
//  30 second response timeout, and the ESP32 cannot be made to do that from here.  The
//  blackhole case connects to an address that never answers, which holds an ESP32 worker for a
//  long time; it shows that the board gives up in bounded time and that the ESP32 is still usable
//  afterwards.  The first two cases and the repeat cases are regression checks.
//
using System;
using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Meadow;
using Meadow.Devices;
using Meadow.Hardware;

public class MeadowApp : App<F7CoreComputeV2>
{
    const string Version = "1";

    //
    //  The bench web server (a small page), and an address on the same network that nobody
    //  answers: SYNs are never acknowledged.
    //
    const string LocalServer = "192.168.1.65";
    const string Blackhole = "10.255.255.1";

    //
    //  A machine on the bench network running idle_server.py, which keeps an idle connection open
    //  for as long as the client likes (the web server above closes one after 60 s).
    //
    const string IdleServer = "192.168.1.65";
    const int IdleServerPort = 8081;

    int cases;
    int failures;

    public override async Task Run()
    {
        Console.WriteLine($"TESTRUN BEGIN espcptests {Version}");

        if (await Case("wifi.connected", WaitForWiFi))
        {
            await Case("socket.send_timeout_default", SendTimeoutDefault);
            await Case("http.get_local", HttpGetLocal);
            await Case("http.repeat_local_x30", () => HttpRepeat(30));
            await Case("tcp.connect_close_x40", () => ConnectCloseRounds(10, 4));
            await Case("tcp.idle_receive_70s", IdleReceive);
            await Case("tcp.connect_blackhole_bounded", ConnectBlackholeBounded);
            await Case("http.get_local_after_blackhole", HttpGetLocal);
            await Case("tcp.connect_close_after_blackhole", () => ConnectCloseRounds(3, 4));
        }
        else
        {
            Console.WriteLine("TESTRUN INFO WiFi is not connected, the network cases were not run");
        }

        Console.WriteLine($"TESTRUN END failures={failures} cases={cases}");

        //
        //  Stay alive so that hcom does not restart the app; the runner ends the session.
        //
        while (true)
        {
            await Task.Delay(60000);
        }
    }

    //
    //  Run one case.  The body returns a detail string, or throws to fail.
    //
    async Task<bool> Case(string name, Func<Task<string>> body)
    {
        cases++;
        var watch = Stopwatch.StartNew();
        try
        {
            var detail = await body();
            Console.WriteLine($"TESTRUN CASE {name} PASS {watch.ElapsedMilliseconds} {detail}");
            return true;
        }
        catch (Exception ex)
        {
            failures++;
            Console.WriteLine($"TESTRUN CASE {name} FAIL {watch.ElapsedMilliseconds} {ex.GetType().Name}: {ex.Message}");
            return false;
        }
    }

    //
    //  IsConnected follows the state events from the ESP32, which are not always raised (plan issues
    //  #703 and #713), so the wireless interface having an IPv4 address also counts as connected.
    //
    async Task<string> WaitForWiFi()
    {
        var wifi = Resolver.Device.NetworkAdapters.Primary<IWiFiNetworkAdapter>();
        if (wifi == null)
        {
            throw new InvalidOperationException("no WiFi adapter");
        }
        for (int i = 0; i < 90; i++)
        {
            var address = WirelessAddress();
            if (address != null)
            {
                return $"ip={address} adapter_state={(wifi.IsConnected ? "connected" : "not-connected")}";
            }
            await Task.Delay(1000);
        }
        throw new TimeoutException($"WiFi did not connect in 90 s (adapter connected={wifi.IsConnected})");
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

    //
    //  espcp sets SO_SNDTIMEO to 10 s on every new ESP32 socket; read it back from the ESP32.
    //
    Task<string> SendTimeoutDefault()
    {
        using (var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
        {
            //
            //  Socket.SendTimeout returns a managed field and never asks the ESP32, so read the option itself.
            //
            int milliseconds = (int) socket.GetSocketOption(SocketOptionLevel.Socket, SocketOptionName.SendTimeout);
            if (milliseconds != 10000)
            {
                throw new InvalidOperationException($"SendTimeout is {milliseconds} ms, expected 10000");
            }
            return Task.FromResult($"sendtimeout={milliseconds}ms");
        }
    }

    //
    //  The first HttpClient request takes about 30 s under the JIT (plan issue #706), so allow for it.
    //
    async Task<string> HttpGetLocal()
    {
        using (var client = new HttpClient { Timeout = TimeSpan.FromSeconds(90) })
        {
            var response = await client.GetAsync($"http://{LocalServer}/");
            var body = await response.Content.ReadAsStringAsync();
            if (!response.IsSuccessStatusCode)
            {
                throw new InvalidOperationException($"status {(int) response.StatusCode}");
            }
            return $"status={(int) response.StatusCode} bytes={body.Length}";
        }
    }

    //
    //  Each request is a poll setup and teardown on the ESP32; a teardown that leaves a poll armed
    //  shows as a failing request after a few passes.
    //
    async Task<string> HttpRepeat(int count)
    {
        using (var client = new HttpClient { Timeout = TimeSpan.FromSeconds(20) })
        {
            for (int i = 0; i < count; i++)
            {
                var response = await client.GetAsync($"http://{LocalServer}/");
                await response.Content.ReadAsStringAsync();
                if (!response.IsSuccessStatusCode)
                {
                    throw new InvalidOperationException($"request {i} status {(int) response.StatusCode}");
                }
            }
        }
        return $"requests={count}";
    }

    //
    //  Open sockets at the web server and close them again.  The ESP32 has about 15 usable
    //  sockets, so a leak shows within a few rounds.  Only a few are held open at once: the bench
    //  web server handles one connection at a time and queues five, and a connect beyond that is
    //  never answered.
    //
    async Task<string> ConnectCloseRounds(int rounds, int perRound)
    {
        for (int round = 0; round < rounds; round++)
        {
            var sockets = new Socket[perRound];
            try
            {
                for (int i = 0; i < perRound; i++)
                {
                    sockets[i] = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                    var connect = Stopwatch.StartNew();
                    try
                    {
                        using (var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(15)))
                        {
                            await sockets[i].ConnectAsync(new IPEndPoint(IPAddress.Parse(LocalServer), 80), cancel.Token);
                        }
                    }
                    catch (Exception ex)
                    {
                        throw new InvalidOperationException($"round {round} socket {i} connect failed after {connect.ElapsedMilliseconds} ms: {ex.GetType().Name}");
                    }
                }
            }
            finally
            {
                foreach (var socket in sockets)
                {
                    socket?.Dispose();
                }
            }
            await Task.Delay(300);
        }
        return $"sockets={rounds * perRound}";
    }

    //
    //  A receive left pending on an idle socket for longer than the 60 s poll timeout must still complete
    //  when data arrives.
    //
    //  What this does and does not show: the .NET socket engine re-polls an idle socket every fraction of a
    //  second (about 480 poll teardowns in a run), so no poll lives for 60 s and the ESP32's poll timeout
    //  does not fire here (the ESP32 log has no "timed out after" line).  The timeout is reached only by a
    //  poll whose teardown was lost, which a test cannot make happen; the ESP32 unit test "A poll with a
    //  timeout ends when it runs out" covers that path.  This case is a check that an idle connection stays
    //  usable across the 60 s mark.
    //
    //
    //  Needs idle_server.py running at IdleServer.
    //
    async Task<string> IdleReceive()
    {
        const int idleSeconds = 70;
        using (var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
        {
            using (var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(15)))
            {
                await socket.ConnectAsync(new IPEndPoint(IPAddress.Parse(IdleServer), IdleServerPort), cancel.Token);
            }

            var buffer = new byte[1024];
            var watch = Stopwatch.StartNew();
            long endedAt = -1;
            var receive = socket.ReceiveAsync(new ArraySegment<byte>(buffer), SocketFlags.None);
            _ = receive.ContinueWith(task => { endedAt = watch.ElapsedMilliseconds; }, TaskScheduler.Default);
            await Task.Delay(idleSeconds * 1000);
            if (receive.IsCompleted)
            {
                //
                //  The time it ended at says who ended it: close to 60 s is the poll timeout, anything else is
                //  more likely the web server closing an idle connection.
                //
                throw new InvalidOperationException($"the receive ended after {endedAt} ms with nothing sent (faulted={receive.IsFaulted}, bytes={(receive.IsCompletedSuccessfully ? receive.Result : -1)})");
            }

            var request = System.Text.Encoding.ASCII.GetBytes($"GET / HTTP/1.0\r\nHost: {IdleServer}\r\n\r\n");
            await socket.SendAsync(new ArraySegment<byte>(request), SocketFlags.None);

            var finished = await Task.WhenAny(receive, Task.Delay(20000));
            if (finished != receive)
            {
                throw new TimeoutException($"no response 20 s after the request, following {idleSeconds} s idle");
            }
            int count = await receive;
            if ((count < 5) || (System.Text.Encoding.ASCII.GetString(buffer, 0, 5) != "HTTP/"))
            {
                throw new InvalidOperationException($"unexpected response, {count} bytes");
            }
            return $"idle={idleSeconds}s response_bytes={count}";
        }
    }

    //
    //  A connect that is never answered must end in bounded time, and the ESP32 must still work
    //  afterwards.  The connect is abandoned after 60 s by the caller; the pass condition is that
    //  the call returns (error or cancel) within 90 s instead of hanging.
    //
    async Task<string> ConnectBlackholeBounded()
    {
        var watch = Stopwatch.StartNew();
        string outcome;
        using (var socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp))
        using (var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(60)))
        {
            try
            {
                await socket.ConnectAsync(new IPEndPoint(IPAddress.Parse(Blackhole), 81), cancel.Token);
                outcome = "connected";
            }
            catch (OperationCanceledException)
            {
                outcome = "cancelled";
            }
            catch (SocketException ex)
            {
                outcome = $"socket error {ex.SocketErrorCode}";
            }
        }
        long seconds = watch.ElapsedMilliseconds / 1000;
        if (outcome == "connected")
        {
            throw new InvalidOperationException($"connected to {Blackhole}, the blackhole address answers");
        }
        if (seconds > 90)
        {
            throw new TimeoutException($"{outcome} after {seconds} s, expected at most 90");
        }
        return $"{outcome} after {seconds}s";
    }
}
