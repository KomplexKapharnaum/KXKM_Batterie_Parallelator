"""Configuration for SOH scoring pipeline — all settings via env vars."""

from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    """Pipeline configuration. All values overridable via SOH_ prefixed env vars."""

    model_config = {"env_prefix": "SOH_"}

    # InfluxDB
    influxdb_url: str = "http://localhost:8086"
    influxdb_token: str = ""
    influxdb_org: str = "kxkm"
    influxdb_bucket: str = "bmu"
    influxdb_output_bucket: str = "bmu"

    # Feature extraction
    etl_window_days: int = 7
    etl_sample_interval_min: int = 30  # 30-min samples over 7 days = ~336 steps

    # TSMixer
    tsmixer_hidden: int = 64
    tsmixer_n_mix_layers: int = 3
    tsmixer_dropout: float = 0.1
    tsmixer_n_features: int = 17  # ETL output feature count
    tsmixer_seq_len: int = 336  # 7 days / 30 min

    # GNN
    gnn_hidden: int = 32
    gnn_n_layers: int = 2
    gnn_heads: int = 4
    gnn_node_features: int = 64  # TSMixer last hidden dim

    # Inference
    tsmixer_onnx_path: str = "models/tsmixer_soh.onnx"
    gnn_onnx_path: str = "models/gnn_fleet.onnx"
    max_batteries: int = 32

    # Scheduling
    scoring_interval_min: int = 30

    # API
    api_host: str = "0.0.0.0"
    api_port: int = 8400

    # Model paths (PyTorch, for training)
    tsmixer_pt_path: str = "models/tsmixer_soh.pt"
    gnn_pt_path: str = "models/gnn_fleet.pt"


settings = Settings()
