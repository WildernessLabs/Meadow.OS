
For CLI 'meadow port select' to see available ports, if only 1, it is used.

To Run from command line
python C:\\WlApps\\PythonTools\\DnldPython\\meadow_file_downloader.py -f C:\\HcomFiles\\Write\\Text\\TestFileGen.txt

To Debug, use this as launch.json - Need to adjust args to match system setup
{
    // Use IntelliSense to learn about possible attributes.
    // Hover to view descriptions of existing attributes.
    // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Python Debugger: Current File with Arguments",
            "type": "debugpy",
            "request": "launch",
            "program": "${file}",
            "console": "integratedTerminal",
            "args": "-f C:\\HcomFiles\\Write\\Text\\TestFileGen.txt"
        }
    ]
}