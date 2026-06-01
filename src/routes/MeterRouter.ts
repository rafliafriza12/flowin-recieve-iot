import express from "express";
import MeterController from "../controllers/MeterController";

const router = express.Router();

router.get("/get-meter", MeterController.getByNomorMeteran);

export default router;
