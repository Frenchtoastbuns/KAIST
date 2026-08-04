import { readFileSync } from "node:fs";
import { runYosys } from "@yowasp/yosys";

const lanes = Number(process.argv[2] ?? 1);
if (![1, 2, 4, 8, 16].includes(lanes))
	throw new Error("usage: node run_parallel_compact_sim.mjs 1|2|4|8|16");

const files = [
	"cap_pdb_parallel_compact_engine.sv",
	"cap_pdb_parallel_compact_tb.sv"
];
const sources = Object.fromEntries(files.map(filename => [
	filename,
	new Uint8Array(readFileSync(new URL(filename, import.meta.url)))
]));
const top = "cap_pdb_parallel_compact_tb";
const script = [
	`read_verilog -sv ${files.join(" ")}`,
	`chparam -set LANES ${lanes} ${top}`,
	`hierarchy -top ${top}`,
	`prep -top ${top}`,
	`sim -clock clk -reset rst -rstlen 2 -n 140 -assert`
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
	output += `\nSIMULATION_EXCEPTION ${error}\n`;
}
process.stdout.write(output);
if (failure)
	process.exitCode = 1;
