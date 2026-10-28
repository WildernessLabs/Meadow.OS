//
//  Cell (PPP) tests: the managed cell API, which reaches the modem through the pppd pause path
//  (meadow_cell_change_state -> hcom_pppd.c -> pppd runs the AT script in its next reconnect).
//  Output follows the test runner contract (tools/testrunner/README.md).
//
//  Needs a BG95-M3 on mikroBUS 1 (COM1, enable pin A3) and, for cell.gnss_fix, a GPS antenna with
//  a view of the sky.  A SIM is not needed for the GNSS and signal cases: without one the modem
//  answers CONNECT and then hangs up, so pppd reconnects about once a minute and runs the posted
//  AT scripts then.
//
//  The data link cases (cell.connected, cell.http_get, cell.http_large) need a SIM and its cell.config.yaml, passed
//  at build time in MEADOW_CELL_CONFIG, which also defines CELL_SIM.  Without CELL_SIM they are
//  skipped.  They run first, because the GNSS and signal requests take the link down.
//
//  The NMEA output contains the board's position, so this app never prints it; it reports only
//  the fix quality, the satellite counts and the dilution of precision.
//
using System;
using System.Diagnostics;
using System.Globalization;
using System.Net.Http;
using System.Threading.Tasks;
using Meadow;
using Meadow.Devices;
using Meadow.Hardware;
using Meadow.Peripherals.Sensors.Location.Gnss;

public class MeadowApp : App<F7CoreComputeV2>
{
    const string Version = "1";

    //
    //  The managed API counts these in seconds.  A request waits for the next pppd reconnect (up to
    //  about 70 s today) and then for the script: the GNSS script alone takes about 150 s.
    //
    const int GnssTimeoutSeconds = 300;
    const int SignalQualityTimeoutSeconds = 120;

    //
    //  Time for the PPP link to come up after boot: modem power-on, the connect script (with
    //  AT+CFUN=1,1 and its 15 s pause), network registration and PPP negotiation.
    //
    const int ConnectTimeoutSeconds = 300;

    //
    //  A small page served over plain HTTP, so that the case tests the link and DNS, not TLS.
    //
    const string HttpUrl = "http://example.com/";

    //
    //  Bodies larger than one TCP segment, so that full-size (about 1400-byte) IP packets cross the
    //  PPP link in both directions: a download, and an upload that the server echoes back.
    //
    const int LargeDownloadBytes = 8192;
    const int LargeUploadBytes = 4096;
    const string LargeDownloadUrl = "http://httpbin.org/bytes/8192";
    const string LargeUploadUrl = "http://httpbin.org/post";

    //
    //  F7CellNetworkAdapter.NoSignal.
    //
    const int NoSignal = -9999;

    int cases;
    int failures;
    int skipped;
    ICellNetworkAdapter cell;
    string gnssOutput;

    public override async Task Run()
    {
        Console.WriteLine($"TESTRUN BEGIN celltests {Version}");

        if (await Case("cell.adapter", FindAdapter))
        {
#if CELL_SIM
            if (await Case("cell.connected", WaitForLink))
            {
                await Case("cell.http_get", HttpGet);
                await Case("cell.http_large", HttpLarge);
            }
#else
            Skip("cell.connected", "no SIM configuration (MEADOW_CELL_CONFIG not set at build time)");
            Skip("cell.http_get", "no SIM configuration (MEADOW_CELL_CONFIG not set at build time)");
            Skip("cell.http_large", "no SIM configuration (MEADOW_CELL_CONFIG not set at build time)");
#endif
            if (await Case("cell.gnss_output", GnssOutput))
            {
                await Case("cell.gnss_fix", GnssFix);
            }
            await Case("cell.signal_quality", SignalQuality);
        }
        else
        {
            Console.WriteLine("TESTRUN INFO no cell adapter, the cell cases were not run");
        }

        Console.WriteLine($"TESTRUN INFO skipped={skipped}");
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

    void Skip(string name, string reason)
    {
        cases++;
        skipped++;
        Console.WriteLine($"TESTRUN CASE {name} SKIP 0 {reason}");
    }

    //
    //  The PPP link is up: the native side reports connected (set by the pppd connect event once
    //  IPCP has finished).
    //
    async Task<string> WaitForLink()
    {
        var watch = Stopwatch.StartNew();
        while (watch.Elapsed.TotalSeconds < ConnectTimeoutSeconds)
        {
            if (cell.IsConnected)
            {
                return $"after={watch.Elapsed.TotalSeconds:F0}s";
            }
            await Task.Delay(1000);
        }
        throw new TimeoutException($"the cell link did not come up in {ConnectTimeoutSeconds} s");
    }

    //
    //  An HTTP GET over the cell link: DNS (the network's servers from IPCP) and TCP through ppp0.
    //
    async Task<string> HttpGet()
    {
        using (var client = new HttpClient())
        {
            client.Timeout = TimeSpan.FromSeconds(60);
            var response = await client.GetAsync(HttpUrl);
            var body = await response.Content.ReadAsStringAsync();
            if (!response.IsSuccessStatusCode)
            {
                throw new InvalidOperationException($"status={(int)response.StatusCode}");
            }
            return $"status={(int)response.StatusCode} bytes={body.Length}";
        }
    }

    //
    //  Full-size packets over the cell link: an 8 KB download and a 4 KB upload.
    //
    async Task<string> HttpLarge()
    {
        using (var client = new HttpClient())
        {
            client.Timeout = TimeSpan.FromSeconds(60);
            var watch = Stopwatch.StartNew();
            var data = await client.GetByteArrayAsync(LargeDownloadUrl);
            long downloadMs = watch.ElapsedMilliseconds;
            if (data.Length != LargeDownloadBytes)
            {
                throw new InvalidOperationException($"download returned {data.Length} bytes, expected {LargeDownloadBytes}");
            }

            var body = new byte[LargeUploadBytes];
            new Random(1).NextBytes(body);
            watch.Restart();
            var response = await client.PostAsync(LargeUploadUrl, new ByteArrayContent(body));
            var echo = await response.Content.ReadAsStringAsync();
            long uploadMs = watch.ElapsedMilliseconds;
            if (!response.IsSuccessStatusCode)
            {
                throw new InvalidOperationException($"upload status={(int)response.StatusCode}");
            }
            if (echo.Length < LargeUploadBytes)
            {
                throw new InvalidOperationException($"upload echo was {echo.Length} characters, expected at least {LargeUploadBytes}");
            }
            return $"download={data.Length}B/{downloadMs}ms upload={LargeUploadBytes}B/{uploadMs}ms echo={echo.Length}";
        }
    }

    Task<string> FindAdapter()
    {
        cell = Resolver.Device.NetworkAdapters.Primary<ICellNetworkAdapter>();
        if (cell == null)
        {
            throw new InvalidOperationException("no cell adapter (is DefaultInterface Cell, and is the OS built with MEADOW_CELL_INCLUDE_IN_BUILD?)");
        }
        return Task.FromResult(cell.GetType().Name);
    }

    //
    //  The GNSS AT script must run and the modem must answer it with NMEA sentences.  This does not
    //  need a fix.  The call blocks, so run it off the app thread.
    //
    async Task<string> GnssOutput()
    {
        gnssOutput = await Task.Run(() => cell.FetchGnssAtCmdsOutput(Array.Empty<IGnssResult>(), GnssTimeoutSeconds));
        if (string.IsNullOrEmpty(gnssOutput))
        {
            throw new TimeoutException($"no AT command output within {GnssTimeoutSeconds} s");
        }
        if ((Sentence(gnssOutput, "$GPGGA") == null) || (Sentence(gnssOutput, "$GPRMC") == null))
        {
            throw new InvalidOperationException($"no GGA/RMC sentence in {gnssOutput.Length} characters of output");
        }
        return $"length={gnssOutput.Length}";
    }

    //
    //  A position fix: GGA fix quality 1 or more and RMC status A.  Needs a GPS antenna with a view
    //  of the sky.
    //
    Task<string> GnssFix()
    {
        var gga = Sentence(gnssOutput, "$GPGGA").Split(',');
        var rmc = Sentence(gnssOutput, "$GPRMC").Split(',');
        var gsa = Sentence(gnssOutput, "$GPGSA")?.Split(',');

        string quality = Field(gga, 6);
        string satellites = Field(gga, 7);
        string hdop = Field(gga, 8);
        string status = Field(rmc, 2);
        string mode = (gsa == null) ? "?" : Field(gsa, 2);

        string detail = $"quality={quality} satellites={satellites} hdop={hdop} rmc_status={status} gsa_mode={mode}";
        bool haveFix = int.TryParse(quality, NumberStyles.Integer, CultureInfo.InvariantCulture, out int q) && (q >= 1);
        if ((!haveFix) || (status != "A"))
        {
            throw new InvalidOperationException($"no fix: {detail}");
        }
        return Task.FromResult(detail);
    }

    //
    //  The signal quality in dBm.  NoSignal means the modem answered +CSQ: 99 (not known) or the
    //  request produced no output.
    //
    async Task<string> SignalQuality()
    {
        int dbm = await Task.Run(() => cell.GetSignalQuality(SignalQualityTimeoutSeconds));
        if (dbm == NoSignal)
        {
            throw new InvalidOperationException("NoSignal (-9999)");
        }
        return $"dbm={dbm}";
    }

    //
    //  The first line of the output that holds the given NMEA sentence, from the sentence on.
    //
    static string Sentence(string output, string tag)
    {
        foreach (var line in output.Split('\n'))
        {
            int start = line.IndexOf(tag, StringComparison.Ordinal);
            if (start >= 0)
            {
                return line.Substring(start).Trim();
            }
        }
        return null;
    }

    static string Field(string[] fields, int index)
    {
        return ((index < fields.Length) && (fields[index].Length > 0)) ? fields[index] : "-";
    }
}
