export const TEMPLATES = {
  welcome:
`# Welcome to Stacks IDE

A lightweight, cross-platform IDE with a Fluent 2020s UI.

## Quick start

- **Ctrl+P** — quick open
- **Ctrl+Shift+P** — command palette
- **Ctrl+S** — save
- **F5** — run active file
- **Ctrl+\`** — toggle bottom panel

## Running code

Stacks IDE looks for the native backend on \`127.0.0.1:17890\`.
When connected, Run uses real compilers and interpreters on your machine:

- Python (\`python3\`)
- Node.js (\`node\`)
- C / C++ (\`gcc\`, \`g++\`, \`clang\`)
- Java (\`javac\` + \`java\`)
- Shell (\`bash\`, \`sh\`)

When offline, JavaScript runs sandboxed in the browser and HTML renders in the preview pane.

Happy hacking — 0xSaad
`,

  html:
`<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Hello from Stacks IDE</title>
  <style>
    body { font: 16px/1.5 system-ui, sans-serif; padding: 2rem; background: #0f172a; color: #e2e8f0; }
    h1 { background: linear-gradient(90deg,#60cdff,#a56eff); -webkit-background-clip: text; color: transparent; }
  </style>
</head>
<body>
  <h1>Hello from Stacks IDE 👋</h1>
  <p>Edit this file and press <b>F5</b> to run the preview.</p>
</body>
</html>
`,

  python:
`#!/usr/bin/env python3
"""Demo script for Stacks IDE."""

def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

if __name__ == "__main__":
    for i in range(10):
        print(f"fib({i}) = {fib(i)}")
`,

  cpp:
`#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string name = argc > 1 ? argv[1] : "world";
    std::cout << "Hello, " << name << "!\\n";
    return 0;
}
`,

  c:
`#include <stdio.h>

int main(void) {
    printf("Hello from C on Stacks IDE\\n");
    return 0;
}
`,

  js:
`// Run me with F5 (sandboxed in the browser when offline)
function greet(name = 'world') {
  console.log('Hello, ' + name + '!');
}
greet('Stacks IDE');
`,

  java:
`public class Main {
    public static void main(String[] args) {
        System.out.println("Hello from Java on Stacks IDE");
    }
}
`,

  shell:
`#!/usr/bin/env bash
set -euo pipefail
echo "Build step running on $(uname -s)"
`,

  md: `# New document\n\nStart writing…\n`,
  json: `{\n  "name": "example",\n  "version": "0.1.0"\n}\n`,
  css: `/* New stylesheet */\nbody { font-family: system-ui, sans-serif; }\n`,
  ts: `export function hello(name: string = "world"): string {\n  return \`Hello, \${name}!\`;\n}\n`,

  byExt(ext) {
    return this[{
      py: "python", js: "js", ts: "ts", cpp: "cpp", c: "c", java: "java",
      sh: "shell", md: "md", json: "json", css: "css", html: "html",
    }[ext]] ?? "";
  },
};
