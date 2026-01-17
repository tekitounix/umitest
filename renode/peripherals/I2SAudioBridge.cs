// I2S Audio Bridge Peripheral for Renode
// Sends audio with virtual time timestamp - no throttling
// Browser handles real-time scheduling based on timestamps

using System;
using System.Net.Sockets;
using Antmicro.Renode.Core;
using Antmicro.Renode.Core.Structure.Registers;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.Sound
{
    public class I2SAudioBridge : BasicDoubleWordPeripheral, IKnownSize
    {
        public I2SAudioBridge(IMachine machine, uint sampleRate = 48000) : base(machine)
        {
            this.sampleRate = sampleRate;
            DefineRegisters();
            this.Log(LogLevel.Info, "I2S Audio Bridge initialized ({0} Hz)", sampleRate);
        }

        public override void Reset()
        {
            base.Reset();
            dmaSendCount = 0;
            audioTimeUs = 0;
        }

        public long Size => 0x30;

        private void DefineRegisters()
        {
            Registers.I2SCFGR.Define(this)
                .WithValueField(0, 16, name: "I2SCFGR",
                    writeCallback: (_, value) =>
                    {
                        i2scfgr = (uint)value;
                        if ((value & 0x0400) != 0)
                        {
                            this.Log(LogLevel.Info, "I2S enabled");
                            ConnectBridge();
                        }
                    },
                    valueProviderCallback: _ => i2scfgr);

            Registers.I2SPR.Define(this)
                .WithValueField(0, 16, name: "I2SPR",
                    writeCallback: (_, value) => i2spr = (uint)value,
                    valueProviderCallback: _ => i2spr);

            Registers.DMA_ADDR.Define(this)
                .WithValueField(0, 32, name: "DMA_ADDR",
                    writeCallback: (_, value) => dmaAddr = value,
                    valueProviderCallback: _ => dmaAddr);

            Registers.DMA_COUNT.Define(this)
                .WithValueField(0, 32, name: "DMA_COUNT",
                    writeCallback: (_, value) => dmaCount = value,
                    valueProviderCallback: _ => dmaCount);

            Registers.DMA_TRIGGER.Define(this)
                .WithValueField(0, 32, name: "DMA_TRIGGER",
                    writeCallback: (_, value) => TriggerDma());
        }

        private void TriggerDma()
        {
            if (dmaAddr == 0 || dmaCount == 0)
                return;

            // Read samples from memory
            var samples = new short[dmaCount];
            for (ulong i = 0; i < dmaCount; i++)
            {
                samples[i] = (short)machine.SystemBus.ReadWord(dmaAddr + i * 2);
            }

            dmaSendCount++;

            // Send with current audio timestamp
            SendToBridge(samples, audioTimeUs);

            // Advance audio time by buffer duration
            // dmaCount is stereo samples, so frames = dmaCount / 2
            ulong frames = dmaCount / 2;
            ulong durationUs = frames * 1000000 / sampleRate;
            audioTimeUs += durationUs;

            if (dmaSendCount <= 5 || dmaSendCount % 1000 == 0)
            {
                this.Log(LogLevel.Info, "DMA[{0}]: {1} samples, audioTime={2}ms",
                    dmaSendCount, dmaCount, audioTimeUs / 1000);
            }
        }

        private void ConnectBridge()
        {
            if (bridgeConnected) return;
            try
            {
                socket = new TcpClient();
                socket.Connect("127.0.0.1", 9001);
                stream = socket.GetStream();
                bridgeConnected = true;
                this.Log(LogLevel.Info, "Connected to bridge");
            }
            catch (Exception e)
            {
                this.Log(LogLevel.Warning, "Bridge error: {0}", e.Message);
            }
        }

        private void SendToBridge(short[] samples, ulong timestampUs)
        {
            if (!bridgeConnected) ConnectBridge();
            if (!bridgeConnected || stream == null) return;

            try
            {
                // Header: "AUD" + version(1) + count(u16) + reserved(u16) + timestamp(u64)
                // Total: 16 bytes header + sample data
                var packet = new byte[16 + samples.Length * 2];
                packet[0] = (byte)'A';
                packet[1] = (byte)'U';
                packet[2] = (byte)'D';
                packet[3] = 1;  // Version 1: includes timestamp
                packet[4] = (byte)(samples.Length & 0xFF);
                packet[5] = (byte)((samples.Length >> 8) & 0xFF);
                packet[6] = 0;
                packet[7] = 0;
                // Timestamp in microseconds (little-endian u64)
                for (int i = 0; i < 8; i++)
                {
                    packet[8 + i] = (byte)((timestampUs >> (i * 8)) & 0xFF);
                }

                Buffer.BlockCopy(samples, 0, packet, 16, samples.Length * 2);
                stream.Write(packet, 0, packet.Length);
            }
            catch
            {
                bridgeConnected = false;
            }
        }

        private readonly uint sampleRate;

        private uint i2scfgr;
        private uint i2spr;
        private ulong dmaAddr;
        private ulong dmaCount;
        private ulong dmaSendCount;
        private ulong audioTimeUs;
        private TcpClient socket;
        private NetworkStream stream;
        private bool bridgeConnected;

        private enum Registers
        {
            I2SCFGR = 0x1C,
            I2SPR = 0x20,
            DMA_ADDR = 0x24,
            DMA_COUNT = 0x28,
            DMA_TRIGGER = 0x2C,
        }
    }
}
