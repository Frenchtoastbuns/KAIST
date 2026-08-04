import { readFileSync } from "node:fs";
import { runYosys } from "@yowasp/yosys";

const filename = "cap_pdb_dual_port_compact_engine.sv";
const sources = {
	[filename]: new Uint8Array(readFileSync(new URL(filename, import.meta.url)))
};
const top = "cap_pdb_dual_port_compact_allocation_engine";
const script = [
	`read_verilog -sv ${filename}`,
	`chparam -set K 64 -set ORDER 4 -set GROUPS 13 -set STATE_BITS 5 -set COST_W 16 -set ACC_W 24 -set SPLIT 48 ${top}`,
	`hierarchy -top ${top}`,
	`check -assert`,
	`synth_xilinx -family xc7 -top ${top}`,
	`check -assert`,
	`stat`
].join("; ");

await runYosys(null);
const decoder = new TextDecoder();
let output = "";
let failure;
try {
	runYosys(["-p", script], sources, {
		synchronously: true,
		stdout: bytes => {
			if (bytes !== null)
				output += decoder.decode(bytes, { stream: true });
		},
		stderr: bytes => {
			if (bytes !== null)
				output += decoder.decode(bytes, { stream: true });
		}
	});
} catch (error) {
	failure = error;
	output += `\nSYNTHESIS_EXCEPTION ${error}\n`;
}
process.stdout.write(output);
if (failure)
	process.exitCode = 1;
