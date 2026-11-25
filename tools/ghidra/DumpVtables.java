// Ghidra headless post-script: for every RTTI vftable whose class name matches a regex,
// prints the function at the given slot offsets and decompiles them.
// args: outFile classRegex slotHex1 slotHex2 ...
// @category TrailerControl
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.regex.Pattern;

public class DumpVtables extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Pattern cls = Pattern.compile(a[1], Pattern.CASE_INSENSITIVE);
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Set<Function> toDecompile = new LinkedHashSet<>();
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                if (!s.getName().contains("vftable")) continue;
                String owner = s.getParentNamespace().getName(true);
                if (!cls.matcher(owner).find()) continue;
                out.println("// vftable " + owner + " @ " + s.getAddress());
                for (int i = 2; i < a.length; i++) {
                    long off = Long.decode(a[i]);
                    Address slot = s.getAddress().add(off);
                    Address target = toAddr(getLong(slot));
                    Function f = getFunctionAt(target);
                    out.println("//   +" + a[i] + " -> " + target + (f == null ? " (no func)" : " " + f.getName()));
                    if (f != null) toDecompile.add(f);
                }
            }
            for (Function f : toDecompile) {
                out.println("// ==================================================");
                out.println("// " + f.getName() + " @ " + f.getEntryPoint());
                for (Function c : f.getCalledFunctions(monitor)) out.println("// callee: " + c.getName() + " @ " + c.getEntryPoint());
                DecompileResults r = dec.decompileFunction(f, 120, monitor);
                out.println(r != null && r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed");
            }
        }
        dec.dispose();
    }
}
