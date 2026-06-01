import { Request, Response } from "express";
import connectDB from "../config/database";

class MeterController {
  public getByNomorMeteran = async (
    req: Request,
    res: Response,
  ): Promise<void> => {
    try {
      const nomorMeteran = req.query.number as string;

      if (!nomorMeteran) {
        res
          .status(400)
          .json({
            status: 400,
            message: "Query parameter 'number' wajib diisi",
          });
        return;
      }

      const mongooseInstance = await connectDB();
      const db = mongooseInstance.connection.db;

      if (!db) {
        res
          .status(500)
          .json({ status: 500, message: "Koneksi database tidak tersedia" });
        return;
      }

      const meter = await db
        .collection("meters")
        .findOne({ NomorMeteran: nomorMeteran });

      if (!meter) {
        res
          .status(404)
          .json({ status: 404, message: "Meteran tidak ditemukan" });
        return;
      }

      res.status(200).json({ status: 200, data: meter });
    } catch (error) {
      console.error("MeterController.getByNomorMeteran error:", error);
      res.status(500).json({ status: 500, message: "Internal server error" });
    }
  };
}

export default new MeterController();
