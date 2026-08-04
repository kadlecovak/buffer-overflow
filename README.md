# Project Overview

This project demonstrates the complete process of exploiting a classic **stack-based buffer overflow** vulnerability on a 32-bit Windows application.

Instead of only presenting the final exploit, the repository documents the entire thought process behind the attack: from understanding the stack layout to constructing custom shellcode capable of executing arbitrary code.

The objective is to **gain control over the Instruction Pointer (EIP)** and redirect program execution toward attacker-controlled machine code located inside the vulnerable input buffer.

The exploit launches:

```text
WinExec("notepad text.txt", 5)
```

which opens a prepared text file using Windows Notepad.

The project consists of the following components:

* **`application.c`**: The target C application containing an intentional buffer overflow vulnerability.
* **`sysfunc.c`**: A helper utility that resolves and prints the local memory address of the `WinExec` API function.
* **`exploit.bin`**: An input file containing our malicious custom code.
* **`text.txt`**: A file to be opened by the exploit in Notepad.

The exploit targets a deliberately vulnerable application compiled without modern mitigation mechanisms such as DEP, ASLR, and stack canaries.

# Vulnerable Application

The target application intentionally uses the unsafe C function `gets()`.
Unlike modern input functions, `gets()` performs **no boundary checking**.

If the user enters more data than the allocated buffer can store, the additional bytes continue writing into adjacent stack memory.

This behavior makes it possible to overwrite:

- local variables,
- saved registers,
- the saved Base Pointer (EBP),
- and eventually the stored Return Address (EIP).

Once the return address becomes attacker-controlled, the program will no longer return to its legitimate caller. Instead, execution can be redirected anywhere in memory.

This vulnerability forms the foundation of the entire exploit developed in this project.

# Exploitation Strategy

The exploit was developed in several consecutive stages.

1. [Stack Layout Analysis](#1-stack-layout-analysis)
2. [Finding the Offset](#2-finding-the-offset)
3. [Payload Placement Strategy](#3-payload-placement-strategy)
4. [Encoding the Return Address](#4-encoding-the-return-address)
5. [Bad Character Analysis](#5-bad-character-analysis)
6. [Dynamic Resolution of WinExec](#6-dynamic-resolution-of-winexec)
7. [Custom x86 Shellcode](#7-custom-x86-shellcode)
8. [Final Payload Construction](#8-final-payload-construction)

Each step is explained in detail below.


# 1. Stack Layout Analysis

Before attempting to overwrite the return address, it is necessary to understand how the vulnerable function organizes its local stack frame.

After entering the function, the stack looked as follows:

```text
0019FEF0   [ Function Arguments ]
0019FEF4   [ Function Arguments / Call ]
0019FEF8   <- ESP, Start of Buffer
...
0019FF24   [ End of Buffer ]
0019FF28   <- Saved EBP
0019FF2C   <- Saved Return Address (EIP)
```

The vulnerable input buffer starts at **ESP** and occupies 48 bytes.

Immediately after the buffer, the function stores:

- the previous Base Pointer (`EBP`),
- followed by the Return Address.

To hijack the program, we must overwrite the Return Address (`EIP`). By mapping the stack, we visualize the layout of the memory and understand the distance between our starting point (the buffer) and our target (`EIP`).

# 2. Finding the Offset

The next task is determining the precise number of bytes required to overwrite the stored return address.

Rather than repeatedly increasing the payload length and hoping to hit the correct position, a unique string was injected into the program.

```
aabbccddeeffgghhiijjkkllmmnnooppqqrrssttuuvvwwxxyyzz112233445566778899
```

After the application crashed, the debugger revealed the overwritten register values.

```
EBP = 7A7A7979 ("yyzz")
EIP = 32323131 ("1122")
```

This immediately identifies which portion of the input reached each register.

### Why this string?
Instead of guessing the distance blindly and crashing the program repeatedly, we send a unique string of characters, where every four-byte sequence appears only once. When the crash occurs, the debugger shows us exactly which four characters landed in the `EIP` register. This gives us the precise byte count (offset) needed to reach and control the execution flow.

# 3. Payload Placement Strategy

Once the exact overwrite offset was known, the next decision was where the shellcode should be stored.

Two possible approaches were considered.

## Option 1 — Place the Shellcode Before the Return Address

```text
+------------------------------+
| Shellcode                    |
| Padding if necessary         |
| New Return Address ----------+----+
+------------------------------+    |
                                    |
                                    |
                                    v
                          Beginning of Buffer
```

In this approach, the shellcode is written directly into the vulnerable buffer. The overwritten return address is replaced with the address of the beginning of that same buffer. When the vulnerable function returns, execution immediately jumps into the injected machine code.

## Option 2 — Place the Shellcode After the Return Address

```text
+------------------------------+
| Padding                      |
| New Return Address ----------+
| Shellcode                    |
+------------------------------+
```

Another possibility is to overwrite the return address with a pointer to shellcode stored later in memory.

We chose for the first option, writing the shellcode directly into the start of the buffer. Once we control `EIP`, we need to point it to our malicious code. By putting our shellcode right at the start of the buffer, we know its exact, predictable memory address (`0019FEF8`).

# 4. Encoding the Return Address

The processor expects addresses to be stored in memory according to the architecture's byte ordering. Since the exploit targets an **Intel x86** processor, addresses are represented using **Little Endian** encoding.

Instead of writing

```text
0019FEF8
```

the bytes must appear in memory as

```text
F8 FE 19 00
``` 

Since we are typing into a Windows console, we converted these hexadecimal values to decimal and used ALT + Numpad codes:
```
F8h -> 248 (ALT+248)

FEh -> 254 (ALT+254)

19h -> 25  (ALT+25)

00h -> 0   (Gets appended automatically by the gets() function as a null-terminator).
```
# 5. Bad Character Analysis

During shellcode development, certain hex characters had to be strictly avoided since we are delivering our payload via a string input function (`gets()`). Sending a byte like `0x0A` (Newline) acts like pressing the "Enter" key. It instantly truncates our payload, rendering the exploit useless. 

These bytes are commonly referred to as **bad characters**.

## Observed Bad Characters
```
0x08 (Dec 8) - Backspace, deletes previous characters in the console.

0x0A (Dec 10) - Newline \n, prematurely terminates gets().

0x1A (Dec 26) - EOF (End of File), prematurely terminates gets().

0x0D (Dec 13) - Carriage Return \r, caused unexpected terminal behavior.
```
# 6. Dynamic Resolution of WinExec

The final payload needs a Windows API function capable of launching another program.

Several alternatives exist, but `WinExec` was chosen because it provides a straightforward interface for executing external applications.

The intended API call is:

```cpp
WinExec("notepad text.txt", 5);
```

which opens the prepared text file in Windows Notepad. We use argument `5` to display Notepad correctly.

---

## Why WinExec?

`WinExec` is exported by **kernel32.dll**, one of the core Windows system libraries. Since nearly every Windows process loads this library automatically, it represents a reliable target for demonstration purposes.

The absolute address of `WinExec` differs between systems. Hardcoding an address would therefore make the exploit work only on one particular machine.

To solve this problem, a small helper program (`sysfunc.c`) was created.

The program uses the Windows API functions:

```cpp
GetModuleHandle()
GetProcAddress()
```

to retrieve the address of `WinExec` dynamically.

On the development machine, the resolved address was

```text
0x76AEF2D0
```

This value is later embedded into the shellcode.

# 7. Custom x86 Shellcode

After controlling the Instruction Pointer (`EIP`), the processor begins executing whatever instructions are located at the address stored in the overwritten return pointer.

Now we need to:

1. construct the string `"notepad text.txt"` in memory,
2. prepare the required function arguments,
3. call `WinExec`,
4. allow Windows to launch Notepad.

To achieve this, a custom x86 shellcode was written entirely in assembly language.
The goal is to execute WinExec("notepad text.txt", 5).

## Initial Idea

Before writing the final shellcode, the desired behavior can be represented in pseudocode.
```
PUSH 5
PUSH &"notepad text.txt"
CALL WinExec
```

## Complete Shellcode

```asm
ADD ESP, -80        ; Adjust stack pointer to prevent shellcode corruption during PUSH operations
XOR EAX, EAX        ; Nullify EAX (creates 0x00 without null bytes in the shellcode)
PUSH EAX            ; Push null terminator for the string
PUSH 0x7478742E     ; Push "txt." (Reversed ".txt")
PUSH 0x74786574     ; Push "txet" (Reversed "text")
PUSH 0x20646170     ; Push " dap" (Reversed "pad ")
PUSH 0x65746F6E     ; Push "eton" (Reversed "note")
MOV EBX, ESP        ; Move pointer to "notepad text.txt" into EBX
ADD EAX, 5          ; Set EAX to 5 (SW_SHOW command for WinExec)
PUSH EAX            ; Push parameter 2: 5
PUSH EBX            ; Push parameter 1: String address
MOV EAX, 0x76AEF2D0 ; Move WinExec address into EAX (Address specific to local system)
CALL EAX            ; Execute WinExec
```

The compiled shellcode occupies **40 bytes**.

# 8. Final Payload Construction
The resulting compiled shellcode is exactly 40 bytes long. We append 12 bytes of padding to reach the 52-byte buffer offset, followed by the Little Endian return address.

When the vulnerable function finishes execution, Windows restores the return address from the stack. Instead of returning to the legitimate caller, execution jumps back into the beginning of our buffer, where the injected shellcode is already waiting.

This completes the exploitation process.

## Final Payload

Hex Representation:
```
83 C4 80 33 C0 50 68 2E 74 78 74 68 74 65 78 74 68 70 61 64 20 68 6E 6F 74 65 8B DC 83 C0 05 50 53 B8 *D0 F2 AE 76* FF D0
```

Where \*D0 F2 AE 76\* is the address of `WinExec`.

# Requirements

- Buffer overflow vulnerability
- DEP disabled
- ASLR disabled
- Stack canaries disabled
- Correct WinExec address
- Existing text.txt

# Running

```bash
application < exploit.bin
```

To test the exploit on another machine, remember to replace the embedded `WinExec` address with the value resolved by sysfunc.c.

