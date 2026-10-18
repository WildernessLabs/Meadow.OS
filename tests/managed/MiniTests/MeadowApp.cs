//
//  Mono's mini test suite (the runtime's JIT regression tests) run on the board as a Meadow app.
//
//  The suites, the exclusions and TestDriver follow the Legacy test runner, so the counts compare
//  with Legacy (733 ran, 2 skipped, 1 failed).  Output follows the test runner contract
//  (tools/testrunner/README.md):
//
//      TESTRUN BEGIN minitests <version>
//      TESTRUN CASE <suite>.<test> PASS|FAIL|SKIP <elapsed-ms> <detail>
//      TESTRUN INFO <text>
//      TESTRUN END failures=<n> cases=<n>
//
//  TestDriver's own "Running '<test>' ..." lines are kept: if a test takes the runtime down, the
//  last of them names it.
//
using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Meadow;
using Meadow.Devices;

public class MeadowApp : App<F7CoreComputeV2>
{
    const string Version = "1";

    static readonly (string Name, Type Type)[] Suites =
    {
        ("basic",       typeof(BasicTests)),
        ("arrays",      typeof(ArrayTests)),
        ("basic-calls", typeof(CallsTests)),
        ("basic-float", typeof(FloatTests)),
        ("basic-long",  typeof(LongTests)),
        ("basic-math",  typeof(MathTests)),
        ("objects",     typeof(ObjectTests.Tests)),
        ("exceptions",  typeof(ExceptionTests)),
        ("generics",    typeof(GenericsTests)),
        ("gshared",     typeof(GSharedTests)),
    };

    //
    //  Excluded as in Legacy: begin_end_invoke (APM is not supported, PlatformNotSupported) and
    //  arm64_vtype_stack_args (crashes in the gsharedvt Thumb-2 trampoline).
    //
    static readonly string[] Arguments =
    {
        "-v",
        "--exclude-test", "begin_end_invoke",
        "--exclude-test", "arm64_vtype_stack_args",
    };

    int cases;
    int failures;

    public override async Task Run()
    {
        Console.WriteLine($"TESTRUN BEGIN minitests {Version}");
        ReportExecutionMode();

        var all = Stopwatch.StartNew();
        foreach (var suite in Suites)
        {
            RunSuite(suite.Name, suite.Type);
        }
        Console.WriteLine($"TESTRUN INFO total {all.ElapsedMilliseconds}ms");

        ProbeCustomAttributes();
        Console.WriteLine($"TESTRUN END failures={failures} cases={cases}");

        //
        //  Stay alive so that hcom does not restart the app; the runner ends the session.
        //
        while (true)
        {
            await Task.Delay(60000);
        }
    }

    void RunSuite(string name, Type type)
    {
        var reporter = new TestDriverReporter();
        reporter.OnCase = (test, status, ms, detail) =>
        {
            cases++;
            if (status == "FAIL")
            {
                failures++;
            }
            Console.WriteLine($"TESTRUN CASE {name}.{test} {status} {ms} {detail}");
        };
        var watch = Stopwatch.StartNew();
        try
        {
            TestDriver.RunTests(type, Arguments, reporter);
            Console.WriteLine($"TESTRUN INFO suite {name} ran={reporter.ExecutedTests} skipped={reporter.SkippedTests} failed={reporter.FailedTests} {watch.ElapsedMilliseconds}ms");
        }
        catch (Exception ex)
        {
            //
            //  TestDriver catches per test, so this is a failure in the driver itself.
            //
            cases++;
            failures++;
            Console.WriteLine($"TESTRUN CASE {name}.suite FAIL {watch.ElapsedMilliseconds} {ex.GetType().Name}: {ex.Message}");
        }
    }

    //
    //  The JIT and the interpreter give the same test results, so show which one ran: a hot loop
    //  takes tens of milliseconds under the JIT and seconds interpreted.
    //
    static void ReportExecutionMode()
    {
        Loop(1000);
        var watch = Stopwatch.StartNew();
        int sum = Loop(2000000);
        Console.WriteLine($"TESTRUN INFO mode-probe loop=2000000 {watch.ElapsedMilliseconds}ms sum={sum}");
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static int Loop(int count)
    {
        int sum = 0;
        for (int i = 0; i < count; i++)
        {
            sum += (i ^ (i >> 3)) & 0xff;
        }
        return sum;
    }

    //
    //  Legacy's TestDriver avoided GetCustomAttributes because it crashed on NuttX 7.
    //
    void ProbeCustomAttributes()
    {
        var watch = Stopwatch.StartNew();
        cases++;
        try
        {
            var methods = typeof(BasicTests).GetMethods();
            int count = 0;
            foreach (var method in methods)
            {
                count += method.GetCustomAttributes(false).Length;
            }
            Console.WriteLine($"TESTRUN CASE probe.get_custom_attributes PASS {watch.ElapsedMilliseconds} methods={methods.Length} attributes={count}");
        }
        catch (Exception ex)
        {
            failures++;
            Console.WriteLine($"TESTRUN CASE probe.get_custom_attributes FAIL {watch.ElapsedMilliseconds} {ex.GetType().Name}: {ex.Message}");
        }
    }
}
