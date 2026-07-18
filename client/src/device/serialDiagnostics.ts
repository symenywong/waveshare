import { z } from 'zod'

const uint32Schema = z.number().int().min(0).max(0xffff_ffff)
const int32Schema = z.number().int().min(-0x8000_0000).max(0x7fff_ffff)

const asrDiagnosticsSchema = z
  .object({
    requestEpoch: uint32Schema,
    phase: z.number().int().min(0).max(5),
    status: z.number().int().min(0).max(8),
    httpStatus: int32Schema,
    transportStatus: int32Schema,
    socketErrno: int32Schema.default(0),
    contentLength: z.number().int().safe(),
    pcmBytes: uint32Schema,
    postBytes: uint32Schema,
    uploadedBytes: uint32Schema,
    uploadWriteCalls: uint32Schema.default(0),
    responseBytes: uint32Schema,
    responseLimit: uint32Schema,
    headerWaitMs: uint32Schema,
    elapsedMs: uint32Schema,
    responseComplete: z.boolean(),
  })
  .catchall(z.union([z.number().finite(), z.boolean()]))

export const helloDiagnosticsSchema = z
  .object({
    snapshotVersion: z.literal(1),
    runtimeReady: z.boolean(),
    runtimeStartPhase: z.number().int().min(0).max(6),
    runtimeStartStatus: int32Schema,
    boardInitPhase: z.number().int().min(0).max(19),
    generation: uint32Schema,
    state: z.enum([
      'BOOT',
      'CONFIG_CHECK',
      'NETWORK_CONNECTING',
      'IDLE',
      'RECORDING',
      'TRANSCRIBING',
      'ASR_JOB_PENDING',
      'THINKING',
      'IDLE_WITH_RESULT',
      'ERROR',
    ]),
    error: z.enum([
      'NONE',
      'CONFIG_MISSING',
      'CONFIG_CORRUPT',
      'NETWORK_FAILED',
      'AUTH_FAILED',
      'TLS_FAILED',
      'CERT_TIME_INVALID',
      'RATE_LIMITED',
      'PROVIDER_UNSUPPORTED',
      'AUDIO_TOO_LONG',
      'ASR_FAILED',
      'CHAT_FAILED',
      'TIMEOUT',
      'CANCELLED',
    ]),
    stateSequence: uint32Schema,
    asr: asrDiagnosticsSchema,
  })
  .catchall(z.union([z.number().finite(), z.boolean()]))
  .nullable()

export type HelloDiagnostics = z.infer<typeof helloDiagnosticsSchema>
