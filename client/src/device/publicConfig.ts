import { z } from 'zod'

const publicConfigSchema = z
  .object({
    revision: z.number().int().positive().max(0xffff_ffff),
    wifi: z
      .object({
        ssid: z.string().min(1).max(32),
        hasPassword: z.boolean(),
      })
      .strict(),
    chatProvider: z.string().min(1).max(31),
    chatModel: z.string().min(1).max(63),
    asrProvider: z.string().min(1).max(31),
    asrModel: z.string().min(1).max(63),
    ttsProvider: z.string().min(1).max(31),
    ttsModel: z.string().min(1).max(63),
    ttsVoice: z.string().min(1).max(31),
    stream: z.boolean(),
    hideReasoning: z.boolean(),
    maxCompletionTokens: z.number().int().positive(),
    hasChatApiKey: z.boolean(),
    hasAsrApiKey: z.boolean(),
    userPrefs: z
      .object({
        volumePercent: z.number().int().min(0).max(100),
        assistantProfile: z
          .object({
            name: z.string().max(23),
            gender: z.enum(['neutral', 'female', 'male']),
          })
          .strict(),
      })
      .strict(),
  })
  .strict()

export type PublicConfig = z.infer<typeof publicConfigSchema>

export function parsePublicConfig(input: unknown): PublicConfig {
  const result = publicConfigSchema.safeParse(input)
  if (!result.success) throw new Error('设备公开配置响应无效')
  return result.data
}
