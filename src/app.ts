import express, { Application, Request, Response } from "express";
import cors from "cors";
import iotRouter from "./routes/IotRouter";
import cronRouter from "./routes/CronRouter";
import meterRouter from "./routes/MeterRouter";

class App {
  public app: Application;

  constructor() {
    this.app = express();
    this.middlewares();
    this.routes();
  }

  private middlewares(): void {
    this.app.use(cors({ origin: "*", optionsSuccessStatus: 200 }));
    this.app.use(express.json());
    this.app.use(express.urlencoded({ extended: true }));
    this.app.use("/iot", iotRouter);
    this.app.use("/api/cron", cronRouter);
    this.app.use("/api", meterRouter);
  }

  private routes(): void {
    this.app.get("/", (_req: Request, res: Response) => {
      res.json({
        message: "Flowin IoT receiver",
        timestamp: new Date().toISOString(),
      });
    });
  }
}

export default new App().app;
