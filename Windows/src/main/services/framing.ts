// ESP32 TCP 通信的帧化协议编解码（纯函数，便于单测）。
//
// 帧格式（大端）：
//   [0] 魔数 0xAA
//   [1] 魔数 0x55
//   [2] 类型（1 字节）
//   [3..6] 负载长度（uint32 大端，不含帧头）
//   [7..] 负载（length 字节）

export const FRAME_MAGIC_0 = 0xaa
export const FRAME_MAGIC_1 = 0x55

export const FrameType = {
  /** 握手帧：负载为 JSON { id, name? }，用于设备识别码校验 */
  HELLO: 0x01,
  /** 文本帧：负载为 UTF-8 JSON 字符串 */
  TEXT: 0x02,
  /** 音频帧：负载为二进制音频数据 */
  AUDIO: 0x03
} as const

export type FrameType = (typeof FrameType)[keyof typeof FrameType]

/** 单帧最大负载长度（4 MiB），防止异常数据撑爆内存 */
export const MAX_FRAME_SIZE = 4 * 1024 * 1024

const HEADER_SIZE = 7

export function encodeFrame(type: FrameType, payload: Buffer): Buffer {
  const header = Buffer.alloc(HEADER_SIZE)
  header[0] = FRAME_MAGIC_0
  header[1] = FRAME_MAGIC_1
  header[2] = type
  header.writeUInt32BE(payload.length, 3)
  return Buffer.concat([header, payload])
}

/** 将任意对象序列化为 JSON 文本帧 */
export function encodeTextFrame(value: unknown): Buffer {
  return encodeFrame(FrameType.TEXT, Buffer.from(JSON.stringify(value), 'utf-8'))
}

export interface DecodedFrame {
  type: FrameType
  payload: Buffer
}

/** 累积字节流并切分出完整帧；遇到魔数错位时逐字节重同步。 */
export class FrameDecoder {
  private buffer: Buffer = Buffer.alloc(0)

  reset(): void {
    this.buffer = Buffer.alloc(0)
  }

  push(chunk: Buffer): DecodedFrame[] {
    this.buffer = this.buffer.length === 0 ? chunk : Buffer.concat([this.buffer, chunk])
    const out: DecodedFrame[] = []

    while (this.buffer.length >= HEADER_SIZE) {
      if (this.buffer[0] !== FRAME_MAGIC_0 || this.buffer[1] !== FRAME_MAGIC_1) {
        this.buffer = this.buffer.subarray(1)
        continue
      }
      const type = this.buffer[2] as FrameType
      const length = this.buffer.readUInt32BE(3)
      if (length > MAX_FRAME_SIZE) {
        // 长度非法，丢弃魔数字节后重同步
        this.buffer = this.buffer.subarray(1)
        continue
      }
      if (this.buffer.length < HEADER_SIZE + length) break
      const payload = this.buffer.subarray(HEADER_SIZE, HEADER_SIZE + length)
      this.buffer = this.buffer.subarray(HEADER_SIZE + length)
      out.push({ type, payload })
    }

    return out
  }
}
