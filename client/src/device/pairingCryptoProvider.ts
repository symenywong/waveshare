import createAiqaPairingModule, {
  type AiqaPairingWasmModule,
} from '../wasm/aiqaPairing.js'

const PAIRING_CODE_LENGTH = 8
const NONCE_LENGTH = 32
const DEVICE_ID_MAX_LENGTH = 32
const ROUND_MAX_LENGTH = 512
const FINISHED_LENGTH = 32
const SECURE_RECORD_MAX_LENGTH = 4096
const UINT32_MAX = 0xffff_ffff
const UINT64_MAX = 0xffff_ffff_ffff_ffffn

export interface PairingMetadata {
  readonly pairingCode: string
  readonly credentialId: number
  readonly handshakeId: bigint
  readonly deviceId: Uint8Array
  readonly clientNonce: Uint8Array
  readonly deviceNonce: Uint8Array
}

export interface PairingCryptoSession {
  writeRoundOne(): Uint8Array
  readRoundOne(message: Uint8Array): void
  writeRoundTwo(): Uint8Array
  readRoundTwo(message: Uint8Array): void
  createFinished(): Uint8Array
  verifyFinished(tag: Uint8Array): void
  encryptRequest(plaintext: Uint8Array): Uint8Array
  decryptResponse(kind: 'response' | 'event', record: Uint8Array): Uint8Array
  destroy(): void
}

export interface PairingCryptoProvider {
  createSession(metadata: PairingMetadata): PairingCryptoSession
}

function validateMetadata(metadata: PairingMetadata): void {
  if (!/^\d{8}$/.test(metadata.pairingCode)) {
    throw new Error('配对码无效')
  }
  if (
    !Number.isInteger(metadata.credentialId) ||
    metadata.credentialId <= 0 ||
    metadata.credentialId > UINT32_MAX ||
    metadata.handshakeId <= 0n ||
    metadata.handshakeId > UINT64_MAX ||
    metadata.deviceId.byteLength === 0 ||
    metadata.deviceId.byteLength > DEVICE_ID_MAX_LENGTH ||
    metadata.clientNonce.byteLength !== NONCE_LENGTH ||
    metadata.deviceNonce.byteLength !== NONCE_LENGTH
  ) {
    throw new Error('配对元数据无效')
  }
}

class WasmAllocation {
  readonly pointer: number

  constructor(
    private readonly module: AiqaPairingWasmModule,
    readonly length: number,
  ) {
    this.pointer = module._malloc(Math.max(length, 1))
    if (this.pointer === 0) throw new Error('配对内存不足')
  }

  write(value: Uint8Array): void {
    if (value.byteLength !== this.length) throw new Error('配对数据长度无效')
    this.module.HEAPU8.set(value, this.pointer)
  }

  read(length = this.length): Uint8Array {
    if (length < 0 || length > this.length) throw new Error('配对数据长度无效')
    return this.module.HEAPU8.slice(this.pointer, this.pointer + length)
  }

  dispose(): void {
    this.module.HEAPU8.fill(0, this.pointer, this.pointer + this.length)
    this.module._free(this.pointer)
  }
}

class WasmPairingSession implements PairingCryptoSession {
  private handle: number

  constructor(
    private readonly module: AiqaPairingWasmModule,
    metadata: PairingMetadata,
  ) {
    validateMetadata(metadata)
    const encoder = new TextEncoder()
    const code = encoder.encode(metadata.pairingCode)
    const allocations: WasmAllocation[] = []
    try {
      allocations.push(new WasmAllocation(module, PAIRING_CODE_LENGTH))
      allocations.push(new WasmAllocation(module, metadata.deviceId.byteLength))
      allocations.push(new WasmAllocation(module, NONCE_LENGTH))
      allocations.push(new WasmAllocation(module, NONCE_LENGTH))
      allocations[0].write(code)
      allocations[1].write(metadata.deviceId)
      allocations[2].write(metadata.clientNonce)
      allocations[3].write(metadata.deviceNonce)
      const high = Number((metadata.handshakeId >> 32n) & 0xffff_ffffn)
      const low = Number(metadata.handshakeId & 0xffff_ffffn)
      this.handle = module._aiqa_wasm_session_create(
        allocations[0].pointer,
        metadata.credentialId,
        high,
        low,
        allocations[1].pointer,
        metadata.deviceId.byteLength,
        allocations[2].pointer,
        allocations[3].pointer,
      )
      if (this.handle <= 0) throw new Error('配对初始化失败')
    } finally {
      code.fill(0)
      for (const allocation of allocations.reverse()) allocation.dispose()
    }
  }

  private ensureOpen(): number {
    if (!Number.isInteger(this.handle) || this.handle <= 0) {
      throw new Error('配对会话已关闭')
    }
    return this.handle
  }

  private terminalFailure(): never {
    this.destroy()
    throw new Error('安全配对失败')
  }

  private writeOutput(
    capacity: number,
    operation: (pointer: number, capacity: number) => number,
  ): Uint8Array {
    this.ensureOpen()
    const output = new WasmAllocation(this.module, capacity)
    try {
      const length = operation(output.pointer, capacity)
      if (length <= 0 || length > capacity) return this.terminalFailure()
      return output.read(length)
    } finally {
      output.dispose()
    }
  }

  private readInput(
    input: Uint8Array,
    operation: (pointer: number, length: number) => number,
  ): void {
    this.ensureOpen()
    const allocation = new WasmAllocation(this.module, input.byteLength)
    try {
      allocation.write(input)
      if (operation(allocation.pointer, input.byteLength) !== 0) {
        this.terminalFailure()
      }
    } finally {
      allocation.dispose()
    }
  }

  writeRoundOne(): Uint8Array {
    const handle = this.ensureOpen()
    return this.writeOutput(ROUND_MAX_LENGTH, (output, capacity) =>
      this.module._aiqa_wasm_write_round_one(handle, output, capacity),
    )
  }

  readRoundOne(message: Uint8Array): void {
    if (message.byteLength === 0 || message.byteLength > ROUND_MAX_LENGTH) {
      return this.terminalFailure()
    }
    const handle = this.ensureOpen()
    this.readInput(message, (input, length) =>
      this.module._aiqa_wasm_read_round_one(handle, input, length),
    )
  }

  writeRoundTwo(): Uint8Array {
    const handle = this.ensureOpen()
    return this.writeOutput(ROUND_MAX_LENGTH, (output, capacity) =>
      this.module._aiqa_wasm_write_round_two(handle, output, capacity),
    )
  }

  readRoundTwo(message: Uint8Array): void {
    if (message.byteLength === 0 || message.byteLength > ROUND_MAX_LENGTH) {
      return this.terminalFailure()
    }
    const handle = this.ensureOpen()
    this.readInput(message, (input, length) =>
      this.module._aiqa_wasm_read_round_two(handle, input, length),
    )
  }

  createFinished(): Uint8Array {
    const handle = this.ensureOpen()
    return this.writeOutput(FINISHED_LENGTH, (output, capacity) =>
      this.module._aiqa_wasm_create_finished(handle, output, capacity),
    )
  }

  verifyFinished(tag: Uint8Array): void {
    if (tag.byteLength !== FINISHED_LENGTH) return this.terminalFailure()
    const handle = this.ensureOpen()
    this.readInput(tag, (input, length) =>
      this.module._aiqa_wasm_verify_finished(handle, input, length),
    )
  }

  encryptRequest(plaintext: Uint8Array): Uint8Array {
    if (plaintext.byteLength > SECURE_RECORD_MAX_LENGTH - 44) {
      return this.terminalFailure()
    }
    const handle = this.ensureOpen()
    const input = new WasmAllocation(this.module, plaintext.byteLength)
    try {
      input.write(plaintext)
      return this.writeOutput(SECURE_RECORD_MAX_LENGTH, (output, capacity) =>
        this.module._aiqa_wasm_encrypt_request(
          handle,
          input.pointer,
          plaintext.byteLength,
          output,
          capacity,
        ),
      )
    } finally {
      input.dispose()
    }
  }

  decryptResponse(kind: 'response' | 'event', record: Uint8Array): Uint8Array {
    if (record.byteLength < 44 || record.byteLength > SECURE_RECORD_MAX_LENGTH) {
      return this.terminalFailure()
    }
    const handle = this.ensureOpen()
    const input = new WasmAllocation(this.module, record.byteLength)
    try {
      input.write(record)
      return this.writeOutput(SECURE_RECORD_MAX_LENGTH - 44, (output, capacity) =>
        this.module._aiqa_wasm_decrypt_response(
          handle,
          kind === 'response' ? 2 : 3,
          input.pointer,
          record.byteLength,
          output,
          capacity,
        ),
      )
    } finally {
      input.dispose()
    }
  }

  destroy(): void {
    if (Number.isInteger(this.handle) && this.handle > 0) {
      this.module._aiqa_wasm_session_destroy(this.handle)
      this.handle = 0
    }
  }
}

export class MbedTlsPairingCryptoProvider implements PairingCryptoProvider {
  constructor(private readonly module: AiqaPairingWasmModule) {
    if (module._aiqa_wasm_api_version() !== 1) {
      throw new Error('配对模块版本不受支持')
    }
  }

  createSession(metadata: PairingMetadata): PairingCryptoSession {
    return new WasmPairingSession(this.module, metadata)
  }
}

export async function loadMbedTlsPairingCryptoProvider(): Promise<PairingCryptoProvider> {
  const module = await createAiqaPairingModule()
  return new MbedTlsPairingCryptoProvider(module)
}
