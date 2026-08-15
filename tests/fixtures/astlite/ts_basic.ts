export function format(title: string, count: number): string {
  return title + ": " + count;
}

export default class Queue {
  private items: number[] = [];
  #length = 0;

  constructor(initial: number[] = []) {
    this.items = initial;
  }

  push(item: number): number {
    this.#bump(1);
    return this.size;
  }

  get size(): number {
    return this.#length;
  }

  #bump(delta: number): void {
    this.#length = this.#length + delta;
  }

  static from(source: number[]): Queue {
    return new Queue(source);
  }
}

export const makeGreeting = (name: string) => name;
const square = n => n * n;
let maxItems = 100;
const run = async (job: Job) => {
  await job.start();
};

function process(input: string): string;
function process(input: number): number;
function process(input: string | number): string | number {
  return format(String(input), square(Number(input)));
}
