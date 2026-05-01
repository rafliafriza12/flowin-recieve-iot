import mongoose, { Schema, Document } from "mongoose";

export interface IRiwayatPenggunaan extends Document {
  MeterID: string;
  UserID: string;
  PenggunaanAir: number;
  timestamp: Date;
}

const RiwayatPenggunaanSchema = new Schema<IRiwayatPenggunaan>({
  MeterID: { type: String, required: true, index: true },
  UserID: { type: String, required: true, index: true },
  PenggunaanAir: { type: Number, required: true },
  timestamp: { type: Date, required: true, index: true },
});

export default mongoose.model<IRiwayatPenggunaan>(
  "RiwayatPenggunaan",
  RiwayatPenggunaanSchema,
  "riwayatpenggunaans"
);
