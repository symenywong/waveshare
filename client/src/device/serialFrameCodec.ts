export const MANAGEMENT_FRAME_MAX_PAYLOAD = 4096
const HEADER_SIZE = 12
const VERSION = 1
const MAGIC = new Uint8Array([0x41, 0x51, 0x4d, 0x47])

export type ManagementFrameKind = 'request' | 'response' | 'event'

export interface ManagementFrame {
  readonly kind: ManagementFrameKind
  readonly payload: Uint8Array
}

const KIND_TO_BYTE: Readonly<Record<ManagementFrameKind, number>> = {
  request: 1,
  response: 2,
  event: 3,
}

function byteToKind(value: number): ManagementFrameKind | null {
  if (value === 1) return 'request'
  if (value === 2) return 'response'
  if (value === 3) return 'event'
  return null
}

export function encodeManagementFrame(
  kind: ManagementFrameKind,
  payload: Uint8Array,
): Uint8Array {
  if (payload.byteLength > MANAGEMENT_FRAME_MAX_PAYLOAD) {
    throw new Error('管理帧过大')
  }
  const frame = new Uint8Array(HEADER_SIZE + payload.byteLength)
  frame.set(MAGIC)
  frame[4] = VERSION
  frame[5] = KIND_TO_BYTE[kind]
  new DataView(frame.buffer).setUint32(8, payload.byteLength, false)
  frame.set(payload, HEADER_SIZE)
  return frame
}

export class ManagementFrameDecoder {
  private readonly header = new Uint8Array(HEADER_SIZE)
  private headerUsed = 0
  private payload = new Uint8Array(0)
  private payloadUsed = 0
  droppedBytes = 0

  push(chunk: Uint8Array): readonly ManagementFrame[] {
    const frames: ManagementFrame[] = []
    for (const byte of chunk) {
      if (this.headerUsed < HEADER_SIZE) {
        this.consumeHeaderByte(byte)
        if (this.headerUsed === HEADER_SIZE && this.payload.byteLength === 0) {
          frames.push(this.finishFrame())
        }
        continue
      }
      this.payload[this.payloadUsed] = byte
      this.payloadUsed += 1
      if (this.payloadUsed === this.payload.byteLength) {
        frames.push(this.finishFrame())
      }
    }
    return frames
  }

  reset(): void {
    this.header.fill(0)
    this.payload.fill(0)
    this.headerUsed = 0
    this.payload = new Uint8Array(0)
    this.payloadUsed = 0
  }

  private consumeHeaderByte(byte: number): void {
    if (this.headerUsed < MAGIC.byteLength) {
      if (byte === MAGIC[this.headerUsed]) {
        this.header[this.headerUsed] = byte
        this.headerUsed += 1
      } else {
        this.droppedBytes += 1
        this.headerUsed = byte === MAGIC[0] ? 1 : 0
        if (this.headerUsed === 1) this.header[0] = byte
      }
      return
    }

    this.header[this.headerUsed] = byte
    this.headerUsed += 1
    if (this.headerUsed !== HEADER_SIZE) return

    const kind = byteToKind(this.header[5] ?? 0)
    const payloadLength = new DataView(this.header.buffer).getUint32(8, false)
    if (
      this.header[4] !== VERSION ||
      kind === null ||
      this.header[6] !== 0 ||
      this.header[7] !== 0 ||
      payloadLength > MANAGEMENT_FRAME_MAX_PAYLOAD
    ) {
      this.reset()
      throw new Error('设备管理帧头无效')
    }
    this.payload = new Uint8Array(payloadLength)
  }

  private finishFrame(): ManagementFrame {
    const kind = byteToKind(this.header[5] ?? 0)
    if (kind === null) {
      this.reset()
      throw new Error('设备管理帧类型无效')
    }
    const frame = { kind, payload: this.payload.slice() } as const
    this.reset()
    return frame
  }
}
