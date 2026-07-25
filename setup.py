from setuptools import setup, Distribution


class BinaryDistribution(Distribution):
    """指示此包包含二进制扩展（.pyd），生成平台特定的 wheel"""
    def has_ext_modules(self):
        return True


setup(distclass=BinaryDistribution)
