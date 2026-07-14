import { z } from 'zod'

const UINT32_MAX = 4_294_967_295
const textEncoder = new TextEncoder()

const ssidSchema = z
  .string()
  .min(1, 'Wi-Fi 名称不能为空')
  .refine((ssid) => !ssid.includes('\u0000'), 'Wi-Fi 名称不能包含空字符')
  .refine((ssid) => textEncoder.encode(ssid).byteLength <= 32, 'Wi-Fi 名称不能超过 32 字节')

const baseSchema = {
  baseRevision: z.number().int().min(1).max(UINT32_MAX),
  ssid: ssidSchema,
}

export const wifiUpdateSchema = z.discriminatedUnion('passwordAction', [
  z
    .object({
      ...baseSchema,
      passwordAction: z.literal('keep'),
      password: z.never().optional(),
    })
    .strict(),
  z
    .object({
      ...baseSchema,
      passwordAction: z.literal('replace'),
      password: z
        .string()
        .refine((password) => !password.includes('\u0000'), 'Wi-Fi 密码不能包含空字符')
        .refine((password) => {
          const length = textEncoder.encode(password).byteLength
          return length >= 8 && length <= 63
        }, 'Wi-Fi 密码必须为 8 到 63 字节'),
    })
    .strict(),
  z
    .object({
      ...baseSchema,
      passwordAction: z.literal('clear'),
      password: z.never().optional(),
    })
    .strict(),
])

export type WifiUpdate = z.infer<typeof wifiUpdateSchema>

export interface WifiPublicConfig {
  readonly revision: number
  readonly ssid: string
  readonly hasPassword: boolean
}

export class WifiValidationError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'WifiValidationError'
  }
}

const wifiPublicConfigSchema = z
  .object({
    revision: z.number().int().min(0).max(UINT32_MAX),
    ssid: ssidSchema,
    hasPassword: z.boolean(),
  })
  .strict()

export function parseWifiPublicConfig(input: unknown): WifiPublicConfig {
  const result = wifiPublicConfigSchema.safeParse(input)
  if (result.success) {
    return result.data
  }
  throw new Error('设备返回的 Wi-Fi 配置无效')
}

export function parseWifiUpdate(input: unknown): WifiUpdate {
  if (
    typeof input === 'object' &&
    input !== null &&
    'passwordAction' in input &&
    (input.passwordAction === 'keep' || input.passwordAction === 'clear') &&
    'password' in input
  ) {
    throw new WifiValidationError(
      input.passwordAction === 'keep'
        ? '保留密码时不应携带密码'
        : '清空密码时不应携带密码',
    )
  }
  const result = wifiUpdateSchema.safeParse(input)
  if (result.success) {
    return result.data
  }
  throw new WifiValidationError(result.error.issues[0]?.message ?? 'Wi-Fi 配置无效')
}
