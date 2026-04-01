package com.kxkm.bmu.di

import com.kxkm.bmu.shared.SharedFactory
import com.kxkm.bmu.shared.auth.AuthUseCase
import com.kxkm.bmu.shared.domain.AuditUseCase
import com.kxkm.bmu.shared.domain.ConfigUseCase
import com.kxkm.bmu.shared.domain.ControlUseCase
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.transport.TransportManager
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object AppModule {

    @Provides
    @Singleton
    fun provideTransportManager(): TransportManager =
        SharedFactory.createTransportManager()

    @Provides
    @Singleton
    fun provideMonitoringUseCase(): MonitoringUseCase =
        SharedFactory.createMonitoringUseCase()

    @Provides
    @Singleton
    fun provideControlUseCase(): ControlUseCase =
        SharedFactory.createControlUseCase()

    @Provides
    @Singleton
    fun provideConfigUseCase(): ConfigUseCase =
        SharedFactory.createConfigUseCase()

    @Provides
    @Singleton
    fun provideAuditUseCase(): AuditUseCase =
        SharedFactory.createAuditUseCase()

    @Provides
    @Singleton
    fun provideAuthUseCase(): AuthUseCase =
        SharedFactory.createAuthUseCase()
}
