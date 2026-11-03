import AVFoundation
import Foundation

/// Owns the bridge handle and renders it through AVAudioEngine.
final class AudioController {
    private let audioEngine = AVAudioEngine()
    private var sourceNode: AVAudioSourceNode!
    let handle: OpaquePointer

    init() throws {
        let deviceFormat = audioEngine.outputNode.inputFormat(forBus: 0)
        let hardwareRate = deviceFormat.sampleRate > 0 ? deviceFormat.sampleRate : 48_000
        let sampleRate = min(max(hardwareRate, 44_100), 192_000)
        guard let created = dl_create(Float(sampleRate)) else {
            throw NSError(domain: "DelayLama", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Could not initialise the synthesis engine."])
        }
        handle = created

        // From here on every stored property is set, so a throw still runs
        // deinit, which releases the handle.
        guard let renderFormat = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2) else {
            throw NSError(domain: "DelayLama", code: 2,
                          userInfo: [NSLocalizedDescriptionKey: "Could not create the audio format."])
        }

        let renderHandle = created
        sourceNode = AVAudioSourceNode(format: renderFormat) { _, _, frameCount, audioBufferList in
            let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
            guard buffers.count >= 2,
                  let leftData = buffers[0].mData,
                  let rightData = buffers[1].mData else {
                for buffer in buffers {
                    if let data = buffer.mData {
                        memset(data, 0, Int(buffer.mDataByteSize))
                    }
                }
                return noErr
            }
            let left = leftData.assumingMemoryBound(to: Float.self)
            let right = rightData.assumingMemoryBound(to: Float.self)
            dl_process(renderHandle, left, right, UInt32(frameCount))
            return noErr
        }

        audioEngine.attach(sourceNode)
        audioEngine.connect(sourceNode, to: audioEngine.mainMixerNode, format: renderFormat)
        audioEngine.mainMixerNode.outputVolume = 0.8
        audioEngine.prepare()
        try audioEngine.start()
    }

    deinit {
        audioEngine.stop()
        dl_destroy(handle)
    }
}
