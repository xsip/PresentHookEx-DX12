![DX12 overlay pipeline](header.png)

# PresentHookEx-DX12

PresentHookEx-DX12 is an external Windows x64 DX12 overlay example designed to work with applications running in fullscreen. `Example.exe` selects a target process, installs a remote `IDXGISwapChain::Present` hook, renders the overlay into a shared texture, and composites that texture onto the target's current back buffer.

The implementation is split across three projects:

- **Example** coordinates target discovery, remote graphics bootstrap, hook installation, renderer startup, recovery, and shutdown.
- **HeadlessRenderer** renders the overlay with D3D12 and ImGui into a shared off-screen texture.
- **LiquidHookEx** supplies process memory operations, remote shellcode execution, and vtable hook installation/restoration.

The DX11 solution is maintained separately in the [PresentHookEx-DX11 repository](https://github.com/xsip/PresentHookEx-DX11).

## Runtime flow

```text
Example.exe
  ├─ finds target process/window and acquires bootstrap DX12 interfaces remotely
  ├─ installs ExecuteCommandLists hook to capture the target's direct queue
  ├─ installs Present hook
  ├─ reads target adapter LUID from the first hooked Present
  ├─ starts HeadlessRenderer on that adapter
  └─ monitors frame progress and restarts the producer if it stalls

Target process
  ├─ ExecuteCommandLists hook remembers the target's direct command queue
  └─ Present hook opens the shared texture/fence, draws the overlay, signals
     the consumer fence value, then calls the original Present
```

## Bootstrap and remote pointer ownership

`Example/Source/Example/Dx12GraphicsProvider.cpp` implements the DX12 graphics provider registered by `Example/main.cpp`. It prepares a plain-layout context and function pointers in the injector, allocates that context and copied shellcode in the target, and starts `DX12GraphicsObtainerThread` remotely. That thread creates a hidden helper window, DXGI factory, D3D12 device, direct queue, and a small swap chain. The returned COM interface pointers are addresses in the target process; they are opaque to the injector and must only be released by the remote release thread.

The helper swap chain and queue are bootstrap objects. They are not assumed to be the application's active chain or rendering queue. The hook machinery uses the helper chain to locate the relevant DXGI Present entry, then observes target `ExecuteCommandLists` calls to capture the application's direct queue.

The remote callbacks are copied into the target as shellcode. Their code and data must therefore be safe to execute at target addresses, and allocations must remain valid for the full lifetime of any remote thread or hook callback. See `LiquidHookEx/README.md` for the general hook engine and shellcode relocation rules.

## Present and queue hooks

`Example/Source/Example/LiquidPresentDx12.cpp` contains the hook implementation and setup:

1. `PresentDx12::Hook` prepares target-side hook data, including interface IIDs, imported API function addresses, shaders, handshake name, and the bootstrap context.
2. The `ID3D12CommandQueue::ExecuteCommandLists` vtable hook records a direct queue pointer in target-side hook data. The pointer is captured in the target process and consumed there.
3. `HookPresent` queries the current chain as `IDXGISwapChain3`, obtains its D3D12 device, and compares it with the captured queue's device.
4. On first use, it opens the named handshake, opens the shared texture and fence, and creates the target-side descriptor heaps, command allocator/list, root signature, and pipeline state.
5. Each Present waits briefly for the producer-ready fence value, gets the current back buffer, records a full-screen textured triangle, submits it on the target's direct queue, signals the consumer-done value, then invokes the original Present.
6. If the swap-chain identity changes, the device is lost, or the renderer handshake generation changes, the hook releases dependent resources and initializes again on a later Present.

Hook callbacks execute in a remote process. Interface pointers in `Dx12PresentHookData` are target-process addresses; the injector must not dereference or release them locally. Callback state is stored in hook data rather than relying on injector globals being directly addressable by copied shellcode.

## Shared texture and fence protocol

The wire format is `DX12Overlay::Handshake` in `HeadlessRenderer/Include/HeadlessRenderer/Dx12OverlayProtocol.h`. The producer publishes a named, read/write file mapping containing the protocol version, target PID, adapter LUID, dimensions, generation, and target-process handle values for the shared texture and fence. The target opens the mapping read-only and imports those shared handles on its matching D3D12 device.

For zero-based frame `N`, the fence values alternate:

| Action | Fence value |
|---|---:|
| Producer finishes rendering the shared texture | `2 * N + 1` |
| Target finishes compositing that texture | `2 * N + 2` |

The producer waits for the consumer-done value before reusing the shared texture. The target waits briefly for the producer-ready value so Present does not block indefinitely on an overlay frame that is not ready yet. The shared-resource generation changes when a renderer is recreated; the target hook observes it and reopens the new resource and fence.

The mapping name defaults to `Local\LiquidOverlayHandshake_DX12_<target PID>`. Set the target executable name in `Example/main.cpp` before running the example.

## Renderer and recovery

`HeadlessRenderer/Source/HeadlessRenderer/HeadlessRendererDX12.cpp` creates a D3D12 device on the adapter LUID reported by the target, renders ImGui into a BGRA texture, and signals the producer-ready fence value. Adapter selection matters: shared D3D12 resources and fences must be opened by a device on the same adapter.

`Example/main.cpp` waits for the first target Present to report its adapter before starting the renderer. It also monitors the target adapter and rendered frame index. If the adapter changes, it restarts the renderer on the new adapter. If the frame index stops advancing, it attempts one renderer restart; the changed handshake generation tells the target hook to rebuild its imported resources.

Moving/resizing a window can coincide with swap-chain or device-resource recreation. The hook checks the current swap-chain identity on Present and reacquires the current back buffer for every composition. Recovery requires Present to continue running so the callback can observe a new handshake generation.

## Build and run

1. Initialize the repository submodules, including `LiquidHookEx` and `HeadlessRenderer/Libraries/imgui`.
2. Open `PresentHookEx-DX12.slnx` in Visual Studio with the Windows SDK and the project toolset installed (`v145` in the project files).
3. Build the **x64** configuration. The `Example` project references `HeadlessRenderer` and `LiquidHookEx`.
4. Set `szExe` in `Example/main.cpp` to the target executable name.
5. Run `Example.exe` with the permissions needed to inspect and modify the target process. The console reports hook setup, adapter selection, renderer readiness, status, recovery, and shutdown.
6. Press **Delete** in the Example console to stop the renderer, unhook, and release the remote bootstrap interfaces.

## Status fields

The Shift status line in `Example/main.cpp` reads `Dx12PresentHookData` from the target and prints:

- `initState`: `1` means initialized; `0` means waiting to initialize/reinitialize; `2` means initialization is in progress.
- `hookError` and `failureStage`: the last hook-side error and the stage where initialization/composition failed. Stage values are assigned in `HookPresent`.
- `presentResult`: result returned by the original Present function.
- `targetDeviceReason`: D3D12 device-removal reason captured by the hook.
- `sharedFence`: last producer-ready value observed by the hook.
- `frame`: number of successfully composited overlay frames.
- `targetLuid` and `overlayLuid`: target and producer adapter identifiers; they should match.

A stable frame counter with no hook error indicates that composition is not completing. Compare the shared fence value with the next producer-ready value (`2 * frame + 1`) and check renderer recovery messages in the console.

## Main implementation files

| File | Purpose |
|---|---|
| `Example/main.cpp` | Target selection, acquisition, adapter wait, renderer lifecycle and recovery |
| `Example/Source/Example/Dx12GraphicsProvider.cpp` | Remote bootstrap graphics acquisition and release |
| `Example/Source/Example/LiquidPresentDx12.cpp` | Present/ExecuteCommandLists hook setup, composition, cleanup and reinitialization |
| `Example/Include/Example/LiquidPresentDx12.h` | Target-side hook state and DX12 data structures |
| `HeadlessRenderer/Source/HeadlessRenderer/HeadlessRendererDX12.cpp` | Producer device, shared overlay, ImGui rendering, fence synchronization and cleanup |
| `HeadlessRenderer/Include/HeadlessRenderer/Dx12OverlayProtocol.h` | Shared handshake layout and producer/consumer fence calculations |
| `LiquidHookEx/LiquidHookEx/Include/LiquidHookEx/VTable.h` | Vtable hook installation, shellcode handling and restoration |

