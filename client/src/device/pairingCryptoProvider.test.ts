import { describe, expect, it, vi } from 'vitest'

import {
  MbedTlsPairingCryptoProvider,
  type PairingMetadata,
} from './pairingCryptoProvider'
import type { AiqaPairingWasmModule } from '../wasm/aiqaPairing.js'

function metadata(): PairingMetadata {
  return {
    pairingCode: '01234567',
    credentialId: 7,
    handshakeId: 0x0102030405060708n,
    deviceId: Uint8Array.from({ length: 32 }, (_, index) => index + 1),
    clientNonce: Uint8Array.from({ length: 32 }, (_, index) => 0x40 + index),
    deviceNonce: Uint8Array.from({ length: 32 }, (_, index) => 0x80 + index),
  }
}

function fakeModule() {
  const heap = new Uint8Array(32_768)
  let cursor = 64
  const freed: Uint8Array[] = []
  const captured: { high?: number; low?: number; code?: string } = {}
  const destroy = vi.fn()
  const module: AiqaPairingWasmModule = {
    HEAPU8: heap,
    _aiqa_wasm_api_version: () => 1,
    _malloc: (length) => {
      const pointer = cursor
      cursor += length + 16
      return pointer
    },
    _free: (pointer) => freed.push(heap.slice(pointer, pointer + 64)),
    _aiqa_wasm_session_create: (
      code,
      _credentialId,
      high,
      low,
    ) => {
      captured.high = high
      captured.low = low
      captured.code = new TextDecoder().decode(heap.slice(code, code + 8))
      return 0x101
    },
    _aiqa_wasm_write_round_one: (_handle, output) => {
      heap.set([1, 2, 3], output)
      return 3
    },
    _aiqa_wasm_read_round_one: () => 0,
    _aiqa_wasm_write_round_two: () => -2,
    _aiqa_wasm_read_round_two: () => 0,
    _aiqa_wasm_create_finished: () => 32,
    _aiqa_wasm_verify_finished: () => 0,
    _aiqa_wasm_encrypt_request: () => -2,
    _aiqa_wasm_decrypt_response: () => -2,
    _aiqa_wasm_session_destroy: destroy,
  }
  return { module, freed, captured, destroy }
}

describe('MbedTlsPairingCryptoProvider', () => {
  it('keeps uint64 handshake metadata exact and clears temporary code memory', () => {
    const fake = fakeModule()
    const provider = new MbedTlsPairingCryptoProvider(fake.module)
    const session = provider.createSession(metadata())

    expect(fake.captured).toEqual({
      high: 0x01020304,
      low: 0x05060708,
      code: '01234567',
    })
    expect(fake.freed[0]?.slice(0, 8)).toEqual(new Uint8Array(8))
    expect(session.writeRoundOne()).toEqual(Uint8Array.from([1, 2, 3]))
    session.destroy()
    session.destroy()
    expect(fake.destroy).toHaveBeenCalledTimes(1)
  })

  it('destroys the opaque session after a terminal native failure', () => {
    const fake = fakeModule()
    const session = new MbedTlsPairingCryptoProvider(fake.module).createSession(metadata())

    expect(() => session.writeRoundTwo()).toThrow('安全配对失败')
    expect(fake.destroy).toHaveBeenCalledTimes(1)
    expect(() => session.writeRoundOne()).toThrow('配对会话已关闭')
  })

  it('rejects malformed codes before allocating a native session', () => {
    const fake = fakeModule()
    const provider = new MbedTlsPairingCryptoProvider(fake.module)
    expect(() => provider.createSession({ ...metadata(), pairingCode: '1234abcd' })).toThrow(
      '配对码无效',
    )
  })

  it.each([
    { credentialId: 0 },
    { credentialId: 1.5 },
    { credentialId: 0x1_0000_0000 },
    { handshakeId: 0n },
    { handshakeId: 0x1_0000_0000_0000_0000n },
    { deviceId: new Uint8Array() },
    { deviceId: new Uint8Array(33) },
    { clientNonce: new Uint8Array(31) },
    { deviceNonce: new Uint8Array(31) },
  ])('rejects invalid pairing metadata %#', (change) => {
    const fake = fakeModule()
    const provider = new MbedTlsPairingCryptoProvider(fake.module)

    expect(() => provider.createSession({ ...metadata(), ...change })).toThrow(
      '配对元数据无效',
    )
  })

  it('rejects an incompatible native API and allocation failure', () => {
    const incompatible = fakeModule()
    incompatible.module._aiqa_wasm_api_version = () => 2
    expect(() => new MbedTlsPairingCryptoProvider(incompatible.module)).toThrow(
      '配对模块版本不受支持',
    )

    const exhausted = fakeModule()
    exhausted.module._malloc = () => 0
    const provider = new MbedTlsPairingCryptoProvider(exhausted.module)
    expect(() => provider.createSession(metadata())).toThrow('配对内存不足')
  })

  it.each([2, 3, 4])(
    'clears every earlier allocation when allocation %i fails',
    (failureAt) => {
      const fake = fakeModule()
      const malloc = fake.module._malloc
      let calls = 0
      fake.module._malloc = (length) => {
        calls += 1
        return calls === failureAt ? 0 : malloc(length)
      }

      expect(() =>
        new MbedTlsPairingCryptoProvider(fake.module).createSession(metadata()),
      ).toThrow('配对内存不足')
      expect(fake.freed).toHaveLength(failureAt - 1)
      expect(fake.freed.every((bytes) => bytes.slice(0, 8).every((byte) => byte === 0))).toBe(
        true,
      )
    },
  )

  it('runs every native operation and validates security boundaries', () => {
    const fake = fakeModule()
    fake.module._aiqa_wasm_write_round_two = (_handle, output) => {
      fake.module.HEAPU8.set([4, 5], output)
      return 2
    }
    fake.module._aiqa_wasm_create_finished = (_handle, output) => {
      fake.module.HEAPU8.fill(6, output, output + 32)
      return 32
    }
    fake.module._aiqa_wasm_encrypt_request = (_handle, _input, _length, output) => {
      fake.module.HEAPU8.fill(7, output, output + 44)
      return 44
    }
    fake.module._aiqa_wasm_decrypt_response = (
      _handle,
      kind,
      _input,
      _length,
      output,
    ) => {
      fake.module.HEAPU8.set([kind, 9], output)
      return 2
    }
    const session = new MbedTlsPairingCryptoProvider(fake.module).createSession(metadata())

    session.readRoundOne(Uint8Array.from([1]))
    expect(session.writeRoundTwo()).toEqual(Uint8Array.from([4, 5]))
    session.readRoundTwo(Uint8Array.from([2]))
    expect(session.createFinished()).toEqual(new Uint8Array(32).fill(6))
    session.verifyFinished(new Uint8Array(32))
    expect(session.encryptRequest(Uint8Array.from([1, 2]))).toEqual(
      new Uint8Array(44).fill(7),
    )
    expect(session.decryptResponse('response', new Uint8Array(44))).toEqual(
      Uint8Array.from([2, 9]),
    )
    expect(session.decryptResponse('event', new Uint8Array(44))).toEqual(
      Uint8Array.from([3, 9]),
    )
  })

  it('destroys sessions after invalid native inputs and authentication failures', () => {
    for (const operation of [
      (session: ReturnType<MbedTlsPairingCryptoProvider['createSession']>) =>
        session.readRoundOne(new Uint8Array()),
      (session: ReturnType<MbedTlsPairingCryptoProvider['createSession']>) =>
        session.readRoundTwo(new Uint8Array(513)),
      (session: ReturnType<MbedTlsPairingCryptoProvider['createSession']>) =>
        session.verifyFinished(new Uint8Array(31)),
      (session: ReturnType<MbedTlsPairingCryptoProvider['createSession']>) =>
        session.encryptRequest(new Uint8Array(4053)),
      (session: ReturnType<MbedTlsPairingCryptoProvider['createSession']>) =>
        session.decryptResponse('response', new Uint8Array(43)),
      (session: ReturnType<MbedTlsPairingCryptoProvider['createSession']>) =>
        session.decryptResponse('response', new Uint8Array(4097)),
    ]) {
      const fake = fakeModule()
      const session = new MbedTlsPairingCryptoProvider(fake.module).createSession(metadata())
      expect(() => operation(session)).toThrow('安全配对失败')
      expect(fake.destroy).toHaveBeenCalledOnce()
    }

    const nativeReject = fakeModule()
    nativeReject.module._aiqa_wasm_read_round_one = () => -1
    const session = new MbedTlsPairingCryptoProvider(nativeReject.module).createSession(
      metadata(),
    )
    expect(() => session.readRoundOne(Uint8Array.from([1]))).toThrow(
      '安全配对失败',
    )
  })
})
