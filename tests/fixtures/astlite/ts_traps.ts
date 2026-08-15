// function commented() { helper(1); }
/* class InComment { run() {} } */
import { extname } from "./path.ts";

const s1 = "function fake() { return call(1); }";
const s2 = 'single "quoted" { braces }';
const tpl = `prefix ${name} and ${deep(`inner ${x}`)} suffix`;
tag`template ${v}`;

interface Handler {
  handle(evt: Event): void;
  label: string;
}

enum Level { Low, High }

const opts = {
  verbose: true,
  run() { logger("object-literal-method"); },
};

class Traps {
  private cb: (e: number) => void = (e) => report(e);
  handler = makeHandler();

  @Component({ selector: "app" })
  render(): void {
    const v = maybe?.value(1);
    if (cond(v)) { report(v); }
  }
}
