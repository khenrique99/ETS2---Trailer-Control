// Ghidra headless post-script: lists every MOV that stores to [reg + disp] for the given
// displacements, grouped by function.
// args: outFile dispHex1 dispHex2 ...
// @category TrailerControl
import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class FindFieldWrites extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Set<Long> disps = new HashSet<>();
        for (int i = 1; i < a.length; i++) disps.add(Long.decode(a[i]));
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (!ins.getMnemonicString().startsWith("MOV")) continue;
                if (ins.getNumOperands() < 2) continue;
                if ((ins.getOperandType(0) & OperandType.DYNAMIC) == 0) continue; // memory dest
                for (Object o : ins.getOpObjects(0)) {
                    if (o instanceof Scalar && disps.contains(((Scalar) o).getUnsignedValue())) {
                        Function f = getFunctionContaining(ins.getAddress());
                        out.println(ins.getAddress() + "  " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint())
                                + "  " + ins);
                    }
                }
            }
        }
    }
}
