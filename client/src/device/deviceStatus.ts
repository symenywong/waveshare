import { z } from 'zod'

const runtimeStateSchema = z.enum([
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
])

const runtimeErrorSchema = z.enum([
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
])

const petExpressionSchema = z.enum([
  'SLEEPY',
  'IDLE',
  'HAPPY',
  'LISTENING',
  'THINKING',
  'SPEAKING',
  'SAD',
  'SHY',
  'FRUSTRATED',
  'BOUNCING',
  'LAUGHING',
  'CRYING',
  'CURIOUS',
  'WORRIED',
])

const managementResultSchema = z.enum([
  'OK',
  'INVALID_REQUEST',
  'FORBIDDEN',
  'NOT_READY',
  'REVISION_CONFLICT',
  'REVISION_EXHAUSTED',
  'BUSY',
  'WIFI_UNREACHABLE_ROLLED_BACK',
  'PERSISTENCE_FAILED_ROLLED_BACK',
  'RECOVERY_REQUIRED',
  'CANCELLED',
  'INTERNAL_ERROR',
])

const managementOperationSchema = z
  .object({
    id: z.number().int().nonnegative().max(4_294_967_295),
    state: z.enum(['NONE', 'PENDING', 'SUCCEEDED', 'FAILED']),
    result: managementResultSchema,
  })
  .strict()

const deviceStatusSchema = z
  .object({
    sequence: z.number().int().nonnegative().max(4_294_967_295),
    state: runtimeStateSchema,
    error: runtimeErrorSchema,
    uptimeMs: z.number().int().nonnegative(),
    freeHeapBytes: z.number().int().nonnegative(),
    wifi: z
      .object({
        connected: z.boolean(),
        rssiDbm: z.number().int().min(-127).max(0).nullable(),
      })
      .strict(),
    power: z
      .object({
        batteryPresent: z.boolean(),
        percent: z.number().int().min(0).max(100).nullable(),
        chargingState: z.enum(['UNKNOWN', 'DISCHARGING', 'ACTIVE', 'DONE']),
      })
      .strict(),
    ui: z
      .object({
        status: z.string().min(1).max(16),
        detail: z.string().min(1).max(32).nullable(),
        hint: z.string().min(1).max(32).nullable(),
        expression: petExpressionSchema,
      })
      .strict(),
    config: z
      .object({
        available: z.boolean(),
        revision: z.number().int().nonnegative().max(4_294_967_295),
        chatProvider: z.string().min(1).max(31).nullable(),
        chatModel: z.string().min(1).max(63).nullable(),
        hasChatApiKey: z.boolean(),
        hasAsrApiKey: z.boolean(),
      })
      .strict(),
    latestOperation: managementOperationSchema,
  })
  .strict()

export type DeviceStatus = z.infer<typeof deviceStatusSchema>

export function parseDeviceStatus(input: unknown): DeviceStatus {
  const result = deviceStatusSchema.safeParse(input)
  if (result.success) {
    return result.data
  }
  throw new Error('设备状态响应无效')
}
