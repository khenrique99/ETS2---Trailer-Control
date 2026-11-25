// Ghidra headless pre-script: turns off analyzers that blow the heap on eurotrucks2.exe
// ("Create Address Tables" OOMs in removeNonSearchableMemory) or that only add noise.
// @category TrailerControl
import ghidra.app.script.GhidraScript;

public class DisableHeavyAnalyzers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] off = { "Create Address Tables", "Embedded Media" };
        for (String name : off) {
            setAnalysisOption(currentProgram, name, "false");
            println("analyzer disabled: " + name);
        }
    }
}
