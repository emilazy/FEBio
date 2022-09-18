packer {
  required_plugins {
    amazon = {
      version = ">= 0.0.2"
      source  = "github.com/hashicorp/amazon"
    }
  }
}

locals {
  buildtime = formatdate("YYYYMMDDhhmm", timestamp())
}

data "amazon-ami" "ubuntu" {
  filters = {
    name             = "ubuntu/images/hvm-ssd/ubuntu-bionic-18.04-amd64-server*"
    root-device-type = "ebs"
  }

  most_recent = true
  owners      = ["099720109477"]
  region      = "us-east-1"
}

source "amazon-ebs" "ubuntu" {
  ami_name      = "packer-provisioned-ubuntu-18.04-intel-oneapi-${local.buildtime}"
  instance_type = "c5a.4xlarge"
  source_ami    = data.amazon-ami.ubuntu.id
  ssh_username  = "ubuntu"

  # skip_create_ami = true

  launch_block_device_mappings {
    device_name           = "/dev/sda1"
    volume_size           = 30
    volume_type           = "gp2"
    delete_on_termination = true
  }
}
variable "image_build_path" {
  type    = string
  default = "/tmp"
}

variable "dependencies_folder" {
  type    = string
  default = "/tmp/linux/dependencies/"
}

build {
  name = "febio"
  sources = [
    "source.amazon-ebs.ubuntu"
  ]

  provisioner "file" {
    source      = "./common/linux"
    destination = var.image_build_path
  }

  provisioner "shell" {
    remote_folder = "${var.image_build_path}/linux"
    script        = "./common/linux/apt.sh"
  }

  # Latest version of cmake (v3.23.2)
  provisioner "shell" {
    script = "./common/linux/cmake.sh"
  }

  # Latest version of git (v3.23.2)
  provisioner "shell" {
    script = "./common/linux/git.sh"
  }

  provisioner "shell" {
    environment_vars = [
      "DEPENDENCIES_PATH=${var.dependencies_folder}"
    ]
    script = "./common/linux/dependencies/install.sh"
  }
}