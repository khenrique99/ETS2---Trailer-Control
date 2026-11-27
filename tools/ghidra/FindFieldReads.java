// Ghidra headless post-script: lists every instruction that READS [reg + disp] for the given
// displacements (mov/lea/cmp/test... with the memory operand as source), grouped by function.
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

public class FindFieldReads extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Set<Long> disps = new HashSet<>();
        for (int i = 1; i < a.length; i++) disps.add(Long.decode(a[i]));
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                String m = ins.getMnemonicString();
                if (m.startsWith("LEA") || m.startsWith("NOP")) continue;
                for (int op = 0; op < ins.getNumOperands(); op++) {
                    if ((ins.getOperandType(op) & OperandType.DYNAMIC) == 0) continue;
                    // For MOV the destination memory operand (op 0) is a write, not a read.
                    if (op == 0 && m.startsWith("MOV")) continue;
                    for (Object o : ins.getOpObjects(op)) {
                        if (o instanceof Scalar && disps.contains(((Scalar) o).getUnsignedValue())) {
                            Function f = getFunctionContaining(ins.getAddress());
                            out.println(ins.getAddress() + "  "
                                    + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()) + "  " + ins);
                        }
                    }
                }
            }
        }
    }
}
