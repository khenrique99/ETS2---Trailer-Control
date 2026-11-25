// Ghidra headless post-script: decompiles the functions containing the given
// addresses (args: outFile addr1 addr2 ...), plus a list of their callers and callees.
// @category TrailerControl
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.PrintWriter;
import java.util.Set;

public class DumpFuncs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DumpFuncs <outFile> <addr...>");
            return;
        }
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(args[i]);
                Function f = getFunctionContaining(a);
                out.println("// ==================================================");
                if (f == null) {
                    out.println("// no function at " + args[i]);
                    continue;
                }
                out.println("// query " + args[i] + " -> " + f.getName() + " @ " + f.getEntryPoint()
                        + " size " + f.getBody().getNumAddresses());
                Set<Function> callers = f.getCallingFunctions(monitor);
                for (Function c : callers) out.println("// caller: " + c.getName() + " @ " + c.getEntryPoint());
                Set<Function> callees = f.getCalledFunctions(monitor);
                for (Function c : callees) out.println("// callee: " + c.getName() + " @ " + c.getEntryPoint());
                DecompileResults r = dec.decompileFunction(f, 180, monitor);
                if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
                else out.println("// decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
            }
        }
        dec.dispose();
    }
}
