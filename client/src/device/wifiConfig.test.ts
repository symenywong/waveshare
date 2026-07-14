import { describe, expect, it } from 'vitest'

import { parseWifiPublicConfig, parseWifiUpdate } from './wifiConfig'

describe('parseWifiUpdate', () => {
  it('accepts valid replace, keep, and clear operations', () => {
    expect(
      parseWifiUpdate({
        baseRevision: 4,
        ssid: 'office',
        passwordAction: 'replace',
        password: '12345678',
      }).passwordAction,
    ).toBe('replace')
    expect(
      parseWifiUpdate({ baseRevision: 4, ssid: 'office', passwordAction: 'keep' })
        .passwordAction,
    ).toBe('keep')
    expect(
      parseWifiUpdate({ baseRevision: 4, ssid: 'guest', passwordAction: 'clear' })
        .passwordAction,
    ).toBe('clear')
  })

  it('validates device byte limits and password length', () => {
    expect(() =>
      parseWifiUpdate({ baseRevision: 1, ssid: '', passwordAction: 'keep' }),
    ).toThrow('Wi-Fi 名称不能为空')
    expect(() =>
      parseWifiUpdate({ baseRevision: 1, ssid: '网'.repeat(11), passwordAction: 'keep' }),
    ).toThrow('32 字节')
    expect(() =>
      parseWifiUpdate({
        baseRevision: 1,
        ssid: 'office',
        passwordAction: 'replace',
        password: 'short',
      }),
    ).toThrow('8 到 63')
    expect(() =>
      parseWifiUpdate({
        baseRevision: 1,
        ssid: 'office\u0000evil',
        passwordAction: 'keep',
      }),
    ).toThrow('不能包含空字符')
    expect(() =>
      parseWifiUpdate({
        baseRevision: 1,
        ssid: 'office',
        passwordAction: 'replace',
        password: '12345678\u0000suffix',
      }),
    ).toThrow('不能包含空字符')
  })

  it('forbids password data when the action does not replace it', () => {
    expect(() =>
      parseWifiUpdate({
        baseRevision: 1,
        ssid: 'office',
        passwordAction: 'keep',
        password: 'must-not-cross-boundary',
      }),
    ).toThrow('不应携带密码')

    expect(() =>
      parseWifiUpdate({
        baseRevision: 1,
        ssid: 'guest',
        passwordAction: 'clear',
        password: 'must-not-cross-boundary',
      }),
    ).toThrow('清空密码时不应携带密码')
  })
})

describe('parseWifiPublicConfig', () => {
  it('accepts a redacted public response and rejects extra secret fields', () => {
    expect(
      parseWifiPublicConfig({ revision: 3, ssid: 'office', hasPassword: true }),
    ).toEqual({ revision: 3, ssid: 'office', hasPassword: true })

    expect(() =>
      parseWifiPublicConfig({
        revision: 3,
        ssid: 'office',
        hasPassword: true,
        password: 'must-not-cross-boundary',
      }),
    ).toThrow('设备返回的 Wi-Fi 配置无效')
  })
})
