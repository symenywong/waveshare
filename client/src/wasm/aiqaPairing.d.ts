export interface AiqaPairingWasmModule {
  readonly HEAPU8: Uint8Array
  _aiqa_wasm_api_version(): number
  _aiqa_wasm_session_create(
    code: number,
    credentialId: number,
    handshakeHigh: number,
    handshakeLow: number,
    deviceId: number,
    deviceIdLength: number,
    clientNonce: number,
    deviceNonce: number,
  ): number
  _aiqa_wasm_write_round_one(handle: number, output: number, capacity: number): number
  _aiqa_wasm_read_round_one(handle: number, input: number, length: number): number
  _aiqa_wasm_write_round_two(handle: number, output: number, capacity: number): number
  _aiqa_wasm_read_round_two(handle: number, input: number, length: number): number
  _aiqa_wasm_create_finished(handle: number, output: number, capacity: number): number
  _aiqa_wasm_verify_finished(handle: number, input: number, length: number): number
  _aiqa_wasm_encrypt_request(
    handle: number,
    input: number,
    length: number,
    output: number,
    capacity: number,
  ): number
  _aiqa_wasm_decrypt_response(
    handle: number,
    kind: number,
    input: number,
    length: number,
    output: number,
    capacity: number,
  ): number
  _aiqa_wasm_session_destroy(handle: number): void
  _malloc(length: number): number
  _free(pointer: number): void
}

export default function createAiqaPairingModule(): Promise<AiqaPairingWasmModule>
