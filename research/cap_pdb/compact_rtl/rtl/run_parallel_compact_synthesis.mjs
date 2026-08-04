import { readFileSync } from "node:fs";
import { runYosys } from "@yowasp/yosys";

const lanes = Number(process.argv[2] ?? 8);
if (![1, 2, 4, 8, 16].includes(lanes))
	throw new Error("usage: node run_parallel_compact_synthesis.mjs 1|2|4|8|16");

const filename = "cap_pdb_parallel_compact_engine.sv";
const sources = {
	[filename]: new Uint8Array(readFileSync(new URL(filename, import.meta.url)))
};
const top = "cap_pdb_parallel_compact_allocation_engine";
const script = [
	`read_verilog -sv ${filename}`,
	`chparam -set K 64 -set ORDER 4 -set GROUPS 13 -set STATE_BITS 5 -set COST_W 16 -set ACC_W 24 -set SPLIT 48 -set LANES ${lanes} ${top}`,
	`hierarchy -top ${top}`,
	`check -assert`,
	`synth_xilinx -family xc7 -top ${top}`,
	`check -assert`,
	`stat`
].join("; ");

// Prefetch first, then execute synchronously.  Node 24 also needs
// --experimental-wasm-exnref for this YoWASP/Yosys build.
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
