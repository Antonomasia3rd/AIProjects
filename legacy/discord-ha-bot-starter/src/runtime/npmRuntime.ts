import path from "node:path";
// Pure resolution policy: no process spawn, config import or environment read.
export function resolveNpmInvocation(platform: string, nodeExe: string,
    environment: Record<string, string | undefined>, exists: (candidate: string) => boolean,
    args: string[]): { file: string; args: string[] } {
  if (platform !== "win32") return { file: "npm", args };
  const p = path.win32, directory = p.dirname(nodeExe);
  const candidates: Array<string | undefined> = [environment.NIFIBOT_NPM_CLI, environment.npm_execpath,
    p.join(directory, "node_modules", "npm", "bin", "npm-cli.js"),
    p.join(directory, "..", "node_modules", "npm", "bin", "npm-cli.js")];
  if (environment.APPDATA) candidates.push(p.join(environment.APPDATA, "npm", "node_modules", "npm", "bin", "npm-cli.js"));
  for (const entry of (environment.Path ?? environment.PATH ?? "").split(";")) {
    const item = entry.trim().replace(/^"|"$/g, "");
    if (item) candidates.push(p.join(item, "node_modules", "npm", "bin", "npm-cli.js"));
  }
  for (const candidate of candidates) if (candidate) {
    const absolute = p.resolve(candidate);
    if (exists(absolute)) return { file: nodeExe, args: [absolute, ...args] };
  }
  throw new Error("Cannot find the installed npm CLI. Set NIFIBOT_NPM_CLI to npm-cli.js or install npm alongside Node. Starting through npm is not required.");
}
