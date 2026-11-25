// Ghidra headless post-script: inside a run of code pointers [lo, hi), lists every slot address
// referenced from executable code (= real vtable starts, written by constructors), with the
// referencing functions. Then, for each start, prints the requested slot targets and decompiles them.
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

public class VtableStarts extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address lo = toAddr(a[1]), hi = toAddr(a[2]);
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        Set<Function> todo = new LinkedHashSet<>();
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (Address s = lo; s.compareTo(hi) < 0; s = s.add(8)) {
                StringBuilder refs = new StringBuilder();
                for (Reference r : getReferencesTo(s)) {
                    MemoryBlock b = getMemoryBlock(r.getFromAddress());
                    if (b == null || !b.isExecute()) continue;
                    Function f = getFunctionContaining(r.getFromAddress());
                    refs.append(" ").append(f == null ? r.getFromAddress().toString() : f.getName());
                }
                if (refs.length() == 0) continue;
                out.println("// vtable start " + s + " (offset " + String.format("0x%x", s.subtract(lo)) + " in run) refs:" + refs);
                for (int i = 3; i < a.length; i++) {
                    long off = Long.decode(a[i]);
                    Address slot = s.add(off);
                    if (slot.compareTo(hi) >= 0) continue;
                    Address tgt = toAddr(getLong(slot));
                    Function f = getFunctionAt(tgt);
                    if (f == null) { createFunction(tgt, null); f = getFunctionAt(tgt); }
                    out.println("//   +" + a[i] + " -> " + tgt + " " + (f == null ? "?" : f.getName() + " params=" + f.getParameterCount()));
                    if (f != null) todo.add(f);
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
