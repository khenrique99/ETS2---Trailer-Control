// Ghidra headless post-script: finds vtables (runs of code pointers in non-exec memory) that
// contain a pointer to any function inside [lo, hi). Prints vtable start, length and the
// requested slots, then decompiles those slot targets.
// args: outFile loHex hiHex slotHex1 slotHex2 ...
// @category TrailerControl
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.TreeSet;

public class FindVtables extends GhidraScript {
    private boolean isCodePtr(Address slot) {
        try {
            Address t = toAddr(getLong(slot));
            MemoryBlock b = getMemoryBlock(t);
            return b != null && b.isExecute();
        } catch (Exception e) {
            return false;
        }
    }

    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address lo = toAddr(a[1]), hi = toAddr(a[2]);
        TreeSet<Address> starts = new TreeSet<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(lo, true)) {
            if (f.getEntryPoint().compareTo(hi) >= 0) break;
            for (Reference r : getReferencesTo(f.getEntryPoint())) {
                Address from = r.getFromAddress();
                MemoryBlock b = getMemoryBlock(from);
                if (b == null || b.isExecute()) continue;
                Address s = from;
                while (isCodePtr(s.subtract(8))) s = s.subtract(8);
                starts.add(s);
            }
        }
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Set<Function> todo = new LinkedHashSet<>();
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (Address s : starts) {
                int n = 0;
                while (isCodePtr(s.add(n * 8L))) n++;
                if (n < 0x40) continue;
                out.println("// vtable @ " + s + " slots " + n + " (" + String.format("0x%x", n * 8) + " bytes)");
                for (int i = 3; i < a.length; i++) {
                    long off = Long.decode(a[i]);
                    if (off / 8 >= n) continue;
                    Address tgt = toAddr(getLong(s.add(off)));
                    Function f = getFunctionAt(tgt);
                    if (f == null) { createFunction(tgt, null); f = getFunctionAt(tgt); }
                    out.println("//   +" + a[i] + " -> " + tgt + " " + (f == null ? "?" : f.getName()));
                    if (f == null) continue;
                    todo.add(f);
                }
            }
            for (Function f : todo) {
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
